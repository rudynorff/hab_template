const axios = require('axios');
const cheerio = require('cheerio');
const fs = require('fs');
const path = require('path');

const ESP_IP = 'http://192.168.4.1'; // The ESP32's default AP IP
const DOWNLOAD_DIR = './mission_photos';

// Ensure download directory exists
if (!fs.existsSync(DOWNLOAD_DIR)) fs.mkdirSync(DOWNLOAD_DIR);

async function downloadMissionData() {
    try {
        console.log(`Connecting to BalloonCam at ${ESP_IP}...`);
        const response = await axios.get(ESP_IP);
        const $ = cheerio.load(response.data);
        
        // Find all links that contain "/download"
        const links = [];
        $('a').each((i, el) => {
            const href = $(el).attr('href');
            if (href && href.includes('/download')) {
                links.push(href);
            }
        });

        console.log(`Found ${links.length} files to download.`);

        // Sequential download loop
        for (let i = 0; i < links.length; i++) {
            const link = links[i];
            // Extract filename from the URL query param
            const urlParams = new URLSearchParams(link.split('?')[1]);
            const fileName = urlParams.get('file').replace('/', '');
            const fileUrl = `${ESP_IP}${link}`;
            
            console.log(`[${i + 1}/${links.length}] Downloading: ${fileName}...`);
            
            const writer = fs.createWriteStream(path.join(DOWNLOAD_DIR, fileName));
            
            const download = await axios({
                url: fileUrl,
                method: 'GET',
                responseType: 'stream'
            });

            download.data.pipe(writer);

            // Wait for the current file to finish before starting the next
            await new Promise((resolve, reject) => {
                writer.on('finish', resolve);
                writer.on('error', reject);
            });
        }

        console.log('✅ All files downloaded successfully!');
    } catch (error) {
        console.error('❌ Error during recovery:', error.message);
    }
}

downloadMissionData();


