const express = require('express');
const bodyParser = require('body-parser');
const fs = require('fs');
const os = require('os');
const path = require('path');

const app = express();
const PORT = 3000;
const CSV_FILE = path.join(__dirname, 'data_log.csv');

// Middleware
app.use(bodyParser.json());

// CORS
app.use((req, res, next) => {
    res.header("Access-Control-Allow-Origin", "*");
    res.header("Access-Control-Allow-Headers", "Content-Type");
    next();
});

// Logging mỗi request
app.use((req, res, next) => {
    console.log(`[${new Date().toISOString()}] ${req.method} ${req.url}`);
    next();
});

// Tạo file CSV với header nếu chưa tồn tại
if (!fs.existsSync(CSV_FILE)) {
    fs.writeFileSync(CSV_FILE, 'timestamp,Vrms_current,Vrms_sensor,Vrms_grid,Irms,P,S,PF\n');
    console.log(`Created new CSV file: ${CSV_FILE}`);
}

// Endpoint nhận dữ liệu từ ESP32
app.post('/data', (req, res) => {
    const { Vrms_current, Vrms_sensor, Vrms_grid, Irms, P, S, PF } = req.body;

    // Validate data
    if (
        typeof Vrms_current !== 'number' ||
        typeof Vrms_sensor !== 'number' ||
        typeof Vrms_grid !== 'number' ||
        typeof Irms !== 'number' ||
        typeof P !== 'number' ||
        typeof S !== 'number' ||
        typeof PF !== 'number'
    ) {
        return res.status(400).json({ message: 'Invalid data' });
    }

    console.log(
        `Received data -> Vrms_current: ${Vrms_current.toFixed(4)} V | Vrms_sensor: ${Vrms_sensor.toFixed(4)} V | Vrms_grid: ${Vrms_grid.toFixed(1)} V | Irms: ${Irms.toFixed(3)} A | P: ${P.toFixed(3)} W | S: ${S.toFixed(3)} VA | PF: ${PF.toFixed(3)}`
    );

    // Ghi vào CSV
    const line = `${new Date().toISOString()},${Vrms_current},${Vrms_sensor},${Vrms_grid},${Irms},${P},${S},${PF}\n`;
    fs.appendFile(CSV_FILE, line, (err) => {
        if (err) console.error('Error writing to file:', err);
    });

    res.json({ message: 'Data received successfully' });
});

// Lấy IP LAN của máy
function getLocalIP() {
    const interfaces = os.networkInterfaces();
    for (let iface in interfaces) {
        for (const addr of interfaces[iface]) {
            if (addr.family === 'IPv4' && !addr.internal) {
                return addr.address;
            }
        }
    }
    return '127.0.0.1';
}

// Start server
app.listen(PORT, '0.0.0.0', () => {
    const ip = getLocalIP();
    console.log(`\n=== ESP32 Backend ===`);
    console.log(`Listening on LAN: http://${ip}:${PORT}`);
    console.log(`ESP32 có thể kết nối tới: http://${ip}:${PORT}/data\n`);
});
