import 'dotenv/config';
import express from 'express';
import mongoose from 'mongoose';
import cors from 'cors';
import helmet from 'helmet';
import morgan from 'morgan';
import http from 'http';
import { Server as SocketIOServer } from 'socket.io';

const app = express();
const server = http.createServer(app);
const io = new SocketIOServer(server, { cors: { origin: '*' } });

// ---------- Middlewares ----------
app.use(helmet({
  contentSecurityPolicy: {
    useDefaults: true,
    directives: {
      "default-src": ["'self'"],
      // ไม่ใช้ CDN → script-src แค่ self
      "script-src": ["'self'"],
      // ถ้ามี <style> inline ใน index.html ให้คง unsafe-inline ไว้ (หรือย้ายไปไฟล์ .css แยก)
      "style-src": ["'self'", "'unsafe-inline'"],
      "img-src": ["'self'", "data:"],
      // ให้หน้าเว็บเชื่อม API/WS โดเมนเดียวกันได้
      "connect-src": ["'self'", "ws:", "wss:"],
      "object-src": ["'none'"],
      "base-uri": ["'self'"],
      "frame-ancestors": ["'self'"]
    }
  },
  crossOriginEmbedderPolicy: false // กันปัญหาบางอย่างเวลาเสิร์ฟไฟล์ static
}));
app.use(cors());
app.use(express.json({ limit: '256kb' }));
app.use(morgan('tiny'));

// ---------- MongoDB ----------
const MONGODB_URI = process.env.MONGODB_URI;
if (!MONGODB_URI) {
  console.error('Missing MONGODB_URI');
  process.exit(1);
}

mongoose
  .connect(MONGODB_URI)
  .then(() => console.log('MongoDB connected'))
  .catch((err) => {
    console.error('Mongo error:', err);
    process.exit(1);
  });

const readingSchema = new mongoose.Schema(
  {
    deviceId: { type: String, default: 'esp32-1', index: true },
    temperature: { type: Number, required: true },
    humidity: { type: Number, required: true },
    // TTL 30 นาที: หมดอายุอัตโนมัติ
    ts: { type: Date, default: Date.now, expires: 1800 }  // <-- เปลี่ยนตรงนี้
  },
  { versionKey: false }
);

// (แนะนำ) คอมพาวด์อินเด็กซ์ให้คิวรีล่าสุดเร็วขึ้น
readingSchema.index({ deviceId: 1, ts: -1 });

const Reading = mongoose.model('Reading', readingSchema);

// ---------- Auth (เฉพาะ API เขียน) ----------
function requireApiKey(req, res, next) {
  const key = req.header('X-API-Key');
  if (!key || key !== process.env.API_KEY) {
    return res.status(401).json({ error: 'Unauthorized' });
  }
  next();
}

// ---------- Routes ----------
app.get('/health', (_req, res) => res.json({ ok: true }));

// เขียนค่า (ป้องกันด้วย API KEY)
app.post('/api/readings', requireApiKey, async (req, res) => {
  try {
    const { temperature, humidity, deviceId } = req.body || {};
    if (typeof temperature !== 'number' || typeof humidity !== 'number') {
      return res.status(400).json({ error: 'temperature & humidity must be numbers' });
    }
    const doc = await Reading.create({
      temperature,
      humidity,
      deviceId: deviceId || 'esp32-1'
    });
    io.emit('new_reading', doc); // broadcast ไปหน้าเว็บแบบ realtime
    return res.json({ ok: true, id: doc._id });
  } catch (e) {
    console.error('POST /api/readings error:', e);
    return res.status(500).json({ error: 'server error' });
  }
});

app.post('/ingest', requireApiKey, async (req, res) => {
  try {
    const { temperature, humidity, device, deviceId } = req.body || {};
    if (typeof temperature !== 'number' || typeof humidity !== 'number') {
      return res.status(400).json({ error: 'temperature & humidity must be numbers' });
    }
    const doc = await Reading.create({
      temperature,
      humidity,
      deviceId: deviceId || device || 'esp32-1'
    });
    io.emit('new_reading', doc);
    return res.json({ ok: true, id: doc._id });
  } catch (e) {
    console.error('POST /ingest error:', e);
    return res.status(500).json({ error: 'server error' });
  }
});


// อ่านค่าใหม่สุด
app.get('/api/readings/latest', async (_req, res) => {
  const doc = await Reading.findOne().sort({ ts: -1 }).lean();
  return res.json(doc ?? {});
});

// อ่านรายการล่าสุด (สำหรับกราฟ)
app.get('/api/readings/recent', async (req, res) => {
  const limit = Math.min(parseInt(req.query.limit || '50', 10), 500);
  const list = await Reading.find().sort({ ts: -1 }).limit(limit).lean();
  return res.json(list.reverse());
});

// เสิร์ฟหน้าเว็บ (public/)
app.use(express.static('public'));

// ---------- Socket.IO ----------
io.on('connection', (socket) => {
  console.log('socket connected', socket.id);
  socket.on('disconnect', () => console.log('socket disconnected', socket.id));
});

// ---------- Start ----------
const PORT = process.env.PORT || 3000;
server.listen(PORT, '0.0.0.0', () => console.log(`Server running on :${PORT}`));

