require('dotenv').config();
const express = require('express');
const bodyParser = require('body-parser');
const os = require('os');
const cors = require('cors');
const morgan = require('morgan');
const fs = require('fs');
const path = require('path');

const app = express();

// Create logs directory if it doesn't exist
const logsDir = path.join(__dirname, 'logs');
if (!fs.existsSync(logsDir)) {
  fs.mkdirSync(logsDir);
}

// Create log streams
const accessLogStream = fs.createWriteStream(path.join(logsDir, 'access.log'), { flags: 'a' });
const errorLogStream = fs.createWriteStream(path.join(logsDir, 'error.log'), { flags: 'a' });

// Custom logging format with timestamp, method, url, status, and response time
morgan.token('timestamp', () => new Date().toISOString());
morgan.token('remote-addr', (req) => {
  return req.headers['x-forwarded-for'] || req.socket.remoteAddress;
});

const logFormat = ':timestamp :remote-addr :method :url :status :res[content-length] - :response-time ms';

// Middleware
app.use(cors());
app.use(bodyParser.json());
app.use(morgan(logFormat, { stream: accessLogStream })); // Log to file
app.use(morgan(logFormat)); // Also log to console

// Error logging middleware
app.use((err, req, res, next) => {
  const errorMessage = `${new Date().toISOString()} - Error: ${err.message}\nStack: ${err.stack}\n`;
  fs.appendFile(path.join(logsDir, 'error.log'), errorMessage, (fsErr) => {
    if (fsErr) console.error('Error writing to error log:', fsErr);
  });
  console.error(errorMessage);
  next(err);
});

// Request body logging middleware
app.use((req, res, next) => {
  if (req.method === 'POST' && req.path === '/api/detections') {
    const requestLog = `${new Date().toISOString()} - Request from ${req.ip}:\nBody: ${JSON.stringify(req.body)}\n`;
    fs.appendFile(path.join(logsDir, 'requests.log'), requestLog, (err) => {
      if (err) console.error('Error writing to requests log:', err);
    });
    console.log('Received detection:', req.body);
  }
  next();
});

// Enhanced IP detection
function getNetworkInfo() {
  const interfaces = os.networkInterfaces();
  const results = {
    localhost: `http://localhost:${process.env.PORT || 3000}`,
    networks: []
  };

  Object.entries(interfaces).forEach(([name, iface]) => {
    iface.forEach(config => {
      if (!config.internal && config.family === 'IPv4') {
        results.networks.push({
          interface: name,
          ip: config.address,
          url: `http://${config.address}:${process.env.PORT || 3000}`
        });
      }
    });
  });

  return results;
}

// API Routes
app.post('/api/detections', (req, res) => {
  try {
    const { frequency, magnitude, timestamp } = req.body;
    
    if (!frequency || !magnitude) {
      const errorMsg = "Missing required fields";
      console.error(`Error processing request from ${req.ip}: ${errorMsg}`);
      return res.status(400).json({ 
        error: errorMsg,
        required: ["frequency", "magnitude"]
      });
    }

    const detection = {
      frequency,
      magnitude,
      timestamp: timestamp || Date.now(),
      receivedAt: new Date().toISOString(),
      clientIp: req.ip
    };

    console.log(`Detection received - Frequency: ${frequency} Hz, Magnitude: ${magnitude}, Client: ${req.ip}`);
    
    // Here you would typically save to a database
    // await Detection.create(detection);

    // Log successful detection to a separate file
    const detectionLog = `${new Date().toISOString()} - Detection: ${JSON.stringify(detection)}\n`;
    fs.appendFile(path.join(logsDir, 'detections.log'), detectionLog, (err) => {
      if (err) console.error('Error writing to detections log:', err);
    });

    res.status(201).json({
      success: true,
      message: "Detection recorded",
      detection
    });
  } catch (error) {
    console.error(`Error processing detection from ${req.ip}:`, error);
    
    // Log error to file
    const errorLog = `${new Date().toISOString()} - Error processing detection from ${req.ip}: ${error.message}\nStack: ${error.stack}\n`;
    fs.appendFile(path.join(logsDir, 'error.log'), errorLog, (err) => {
      if (err) console.error('Error writing to error log:', err);
    });
    
    res.status(500).json({ 
      error: "Internal server error",
      details: error.message 
    });
  }
});

// Health check endpoint with detailed information
app.get('/api/health', (req, res) => {
  const healthInfo = {
    status: 'healthy',
    timestamp: new Date().toISOString(),
    uptime: process.uptime(),
    memory: process.memoryUsage(),
    clientIp: req.ip,
    serverIp: getNetworkInfo()
  };
  
  console.log(`Health check from ${req.ip}`);
  
  res.status(200).json(healthInfo);
});

// Start server
const PORT = process.env.PORT || 3000;
const networkInfo = getNetworkInfo();

app.listen(PORT, '0.0.0.0', () => {
  console.log('\n\x1b[36m%s\x1b[0m', '=== FFT Detection Server ==='); // Cyan colored title
  console.log('\n\x1b[32m%s\x1b[0m', 'Server is running!');
  console.log(`\nLocal access: \x1b[33m${networkInfo.localhost}\x1b[0m`);
  
  if (networkInfo.networks.length > 0) {
    console.log('\nNetwork access:');
    networkInfo.networks.forEach(net => {
      console.log(`- ${net.interface}: \x1b[33m${net.url}\x1b[0m`);
    });
  } else {
    console.log('\n\x1b[31mNo network interfaces found!\x1b[0m');
  }

  console.log('\nAvailable endpoints:');
  console.log(`- POST \x1b[33m/api/detections\x1b[0m`);
  console.log(`- GET  \x1b[33m/health\x1b[0m`);
  console.log('\nLogs directory:', logsDir);
  console.log('\nPress CTRL+C to stop\n');
});