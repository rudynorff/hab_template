const axios = require('axios');
const { off } = require('cluster');
const fs = require('fs');
const path = require('path');

// Get arguments from command line: npm run start: npm run start -- <session_id> <photo_count> [<offset> <lastPhotoIndex>]
const sessionId = process.argv[4];
const photoCount = process.argv[7] || process.argv[5];
const offset = process.argv[6] || 0;
const ESP_IP = 'http://192.168.4.1';
const DOWNLOAD_DIR = `./mission_${sessionId}`;

if (!sessionId || !photoCount) {
    console.error("Usage:\nnpm run start -- <session_id> <photo_count> [<offset> <lastPhotoIndex>]");
    process.exit(1);
}

console.log(`sessionId ${sessionId}; photoCount ${photoCount}; offset ${offset}`);

if (!fs.existsSync(DOWNLOAD_DIR)) fs.mkdirSync(DOWNLOAD_DIR);

async function startDownload() {
    console.log(`🚀 Starting recovery for Session ${sessionId} (${photoCount} photos)`);

    for (let i = offset; i < photoCount; i++) {
        const fileName = `pic_${sessionId}_${i}.jpg`;
        const fileUrl = `${ESP_IP}/download?file=/${fileName}`;
        const localPath = path.join(DOWNLOAD_DIR, fileName);

        try {
            console.log(`[${i + 1}/${photoCount}] Fetching ${fileName}...`);
            const writer = fs.createWriteStream(localPath);
            
            const response = await axios({
                url: fileUrl,
                method: 'GET',
                responseType: 'stream',
                timeout: 10000 // 10 second timeout per photo
            });

            response.data.pipe(writer);

            await new Promise((resolve, reject) => {
                writer.on('finish', resolve);
                writer.on('error', reject);
            });
        } catch (err) {
            console.error(`❌ Failed to download ${fileName}: ${err.message}`);
            // If the photo is missing, we continue to the next one
        }
    }
    console.log("✅ Recovery complete.");
}

startDownload();