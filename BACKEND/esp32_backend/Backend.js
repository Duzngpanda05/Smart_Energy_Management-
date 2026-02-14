const express = require("express");
const fs = require("fs");
const path = require("path");

const app = express();
const PORT = 3000;

app.use(express.json());

/* ================= FILE PATH ================= */
const DATA_CSV = path.join(__dirname, "nilm_log.csv");

/* ================= INIT CSV ================= */
if (!fs.existsSync(DATA_CSV)) {
    fs.writeFileSync(
        DATA_CSV,
        "Irms,Vrms,P,Q,label\n"
    );
    console.log("Created nilm_log.csv");
}

/* =======================================================
 *                    RECEIVE DATA
 * ======================================================= */
app.post("/data", (req, res) => {
    try {
        const { Irms, Vrms, P, Q, label } = req.body;

        if (
            Irms === undefined ||
            Vrms === undefined ||
            P === undefined ||
            Q === undefined ||
            label === undefined
        ) {
            return res.status(400).json({ message: "Missing data fields" });
        }

        const timestamp = new Date().toISOString();
        const line = `${timestamp},${Irms},${Vrms},${P},${Q},${label}\n`;

        fs.appendFileSync(DATA_CSV, line);

        console.log("[DATA]", timestamp, req.body);
        res.json({ message: "Data received" });

    } catch (err) {
        console.error(err);
        res.status(500).json({ message: "Server error" });
    }
});

/* ================= START SERVER ================= */
/* ==================== START SERVER ==================== */
app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});