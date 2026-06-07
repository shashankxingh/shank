const express = require('express');
const path = require('path');
const app = express();

const PORT = process.env.PORT || 8080;

// Serve the HTML and CSS files normally
app.use(express.static(path.join(__dirname)));

// Specific route for the download to force the attachment header
app.get('/downloads/Shank_Setup_v0.1.0_win64.exe', (req, res) => {
    const file = path.join(__dirname, 'downloads', 'Shank_Setup_v0.1.0_win64.exe');
    res.download(file, 'Shank_Setup_v0.1.0_win64.exe', (err) => {
        if (err) {
            console.error("Error downloading file:", err);
            res.status(404).send("File not found");
        }
    });
});

app.listen(PORT, () => {
    console.log(`Server is running on port ${PORT}`);
});
