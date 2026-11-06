#include "StorageHAL.h"
#include "default_apps.h"
#include "core/Logger.h"

// Static member definitions
bool StorageHAL::s_littlefsMounted = false;
bool StorageHAL::s_sdMounted = false;
SPIClass StorageHAL::s_spiSD = SPIClass(VSPI);

bool StorageHAL::init() {
    Serial.println("[StorageHAL] Initializing Storage HAL...");

    // 1. Mount LittleFS on internal SPI flash (partition subtype: spiffs, label: "spiffs")
    // formatOnFail = true ensures fresh/unformatted flash is initialized cleanly on first boot
    s_littlefsMounted = LittleFS.begin(true, "/littlefs", 10, "spiffs");

    if (s_littlefsMounted) {
        core::Logger::flushEarlyLogs();
        Serial.printf("[StorageHAL] LittleFS mounted successfully. Capacity: %u KB, Used: %u KB\n",
                      (unsigned int)(LittleFS.totalBytes() / 1024),
                      (unsigned int)(LittleFS.usedBytes() / 1024));
        LOG_INFO("STORAGE", "LittleFS mounted (%u/%u KB used)",
                 (unsigned int)(LittleFS.usedBytes() / 1024),
                 (unsigned int)(LittleFS.totalBytes() / 1024));

        // Ensure default application directory exists
        ensureDirectory("/apps");

        // Execute turnkey auto-provisioning for bundled apps
        provisionDefaultApps();
    } else {
        Serial.println("[StorageHAL] CRITICAL: Failed to mount LittleFS!");
        LOG_FATAL("STORAGE", "LittleFS mount failed!");
    }

    // 2. Probe for MicroSD card on dedicated VSPI pins (optional secondary storage)
    probeSDCard();

    return s_littlefsMounted;
}

void StorageHAL::end() {
    if (s_sdMounted) {
        SD.end();
        s_spiSD.end();
        s_sdMounted = false;
        Serial.println("[StorageHAL] MicroSD unmounted.");
    }

    if (s_littlefsMounted) {
        LittleFS.end();
        s_littlefsMounted = false;
        Serial.println("[StorageHAL] LittleFS unmounted.");
    }
}

bool StorageHAL::isLittleFSMounted() {
    return s_littlefsMounted;
}

bool StorageHAL::isSDMounted() {
    return s_sdMounted;
}

size_t StorageHAL::getStorageTotalBytes() {
    return s_littlefsMounted ? LittleFS.totalBytes() : 0;
}

size_t StorageHAL::getStorageUsedBytes() {
    return s_littlefsMounted ? LittleFS.usedBytes() : 0;
}

size_t StorageHAL::getLittleFSTotalBytes() {
    return s_littlefsMounted ? LittleFS.totalBytes() : 0;
}

size_t StorageHAL::getLittleFSUsedBytes() {
    return s_littlefsMounted ? LittleFS.usedBytes() : 0;
}

size_t StorageHAL::getSDTotalBytes() {
    return s_sdMounted ? SD.totalBytes() : 0;
}

size_t StorageHAL::getSDUsedBytes() {
    return s_sdMounted ? SD.usedBytes() : 0;
}

uint8_t StorageHAL::getSDCardType() {
    return s_sdMounted ? SD.cardType() : CARD_NONE;
}

bool StorageHAL::probeSDCard() {
    if (s_sdMounted) {
        return true;
    }

    pinMode(CYD_SD_CS, OUTPUT);
    digitalWrite(CYD_SD_CS, HIGH);

    Serial.println("[StorageHAL] Probing for MicroSD on dedicated VSPI (CS: 5, CLK: 18, MISO: 19, MOSI: 23)...");

    // Initialize dedicated SPI pins for SD card
    s_spiSD.begin(CYD_SD_CLK, CYD_SD_MISO, CYD_SD_MOSI, CYD_SD_CS);

    // Attempt SD mount at 4 MHz
    if (SD.begin(CYD_SD_CS, s_spiSD, 4000000, "/sd")) {
        uint8_t type = SD.cardType();
        if (type != CARD_NONE) {
            s_sdMounted = true;
            const char* typeStr = "UNKNOWN";
            if (type == CARD_MMC)  typeStr = "MMC";
            else if (type == CARD_SD)   typeStr = "SDSC";
            else if (type == CARD_SDHC) typeStr = "SDHC";

            Serial.printf("[StorageHAL] MicroSD card detected (%s). Size: %llu MB\n",
                          typeStr, SD.cardSize() / (1024 * 1024));

            ensureDirectory("/sd/apps");
            LOG_INFO("STORAGE", "MicroSD mounted (%s, %llu MB) -> copying kernel.log",
                     typeStr, SD.cardSize() / (1024 * 1024));
            core::Logger::syncToSD();
            return true;
        }
        SD.end();
    }

    s_sdMounted = false;
    Serial.println("[StorageHAL] MicroSD card not present. Operating with LittleFS only.");
    LOG_WARN("STORAGE", "MicroSD not detected; running on Flash only");
    return false;
}

bool StorageHAL::ensureDirectory(const char* dirPath) {
    if (!dirPath || strlen(dirPath) == 0) return false;

    if (isSDPath(dirPath) && !s_sdMounted) {
        return false;
    }

    fs::FS &fs = getFSForPath(dirPath);
    const char* target = isSDPath(dirPath) ? stripSDPrefix(dirPath) : dirPath;

    // Root directory always exists
    if (strcmp(target, "/") == 0 || strlen(target) == 0) return true;

    if (!fs.exists(target)) {
        bool ok = fs.mkdir(target);
        if (ok) {
            Serial.printf("[StorageHAL] Created directory: %s\n", dirPath);
        } else {
            Serial.printf("[StorageHAL] Failed to create directory: %s\n", dirPath);
        }
        return ok;
    }
    return true;
}

void StorageHAL::writeProgmemFile(const char* path, const char* progmemData) {
    if (!s_littlefsMounted) return;

    File f = LittleFS.open(path, "w");
    if (!f) {
        Serial.printf("[StorageHAL] ERROR: Could not open %s for writing!\n", path);
        LOG_ERROR("STORAGE", "Failed to open %s for provisioning", path);
        return;
    }

    size_t totalLen = strlen_P(progmemData);
    size_t written = 0;
    const size_t CHUNK_SIZE = 256;
    char buffer[CHUNK_SIZE + 1];

    while (written < totalLen) {
        size_t toWrite = totalLen - written;
        if (toWrite > CHUNK_SIZE) {
            toWrite = CHUNK_SIZE;
        }
        strncpy_P(buffer, progmemData + written, toWrite);
        buffer[toWrite] = '\0';

        size_t bytesWritten = f.write((const uint8_t*)buffer, toWrite);
        if (bytesWritten != toWrite) {
            Serial.printf("[StorageHAL] ERROR: Write mismatch on %s! Expected %u, wrote %u\n",
                          path, (unsigned int)toWrite, (unsigned int)bytesWritten);
            LOG_ERROR("STORAGE", "Write mismatch on %s (%u/%u B)", path, (unsigned int)bytesWritten, (unsigned int)toWrite);
            break;
        }
        written += toWrite;
    }
    f.flush();
    f.close();
    Serial.printf("[StorageHAL] Provisioned %s successfully (%u bytes written)\n",
                  path, (unsigned int)written);
    LOG_INFO("STORAGE", "Provisioned %s (%u B)", path, (unsigned int)written);
}

void StorageHAL::provisionDefaultApps() {
    if (!s_littlefsMounted) return;

    ensureDirectory("/apps");

    for (size_t i = 0; i < NUM_BUNDLED_APP_SCRIPTS; i++) {
        const char* path = BUNDLED_APP_SCRIPTS[i].filePath;
        const char* data = BUNDLED_APP_SCRIPTS[i].progmemData;

        bool needsProvision = false;
        if (!LittleFS.exists(path)) {
            needsProvision = true;
        } else {
            File existing = LittleFS.open(path, "r");
            size_t progmemLen = strlen_P(data);
            if (!existing || existing.size() != progmemLen) {
                needsProvision = true;
            } else {
                // Verify content matches PROGMEM
                String cur = existing.readString();
                if (memcmp_P(cur.c_str(), data, progmemLen) != 0) {
                    needsProvision = true;
                }
            }
            if (existing) existing.close();
        }

        if (needsProvision) {
            Serial.printf("[StorageHAL] Auto-provisioning missing app: %s...\n", path);
            writeProgmemFile(path, data);
        } else {
            Serial.printf("[StorageHAL] App %s verified on LittleFS (skipping overwrite).\n", path);
        }
    }
}

void StorageHAL::forceProvisionDefaultApps() {
    if (!s_littlefsMounted) return;

    Serial.println("[StorageHAL] Factory reset: force-reprovisioning all default apps...");
    ensureDirectory("/apps");

    for (size_t i = 0; i < NUM_BUNDLED_APP_SCRIPTS; i++) {
        const char* path = BUNDLED_APP_SCRIPTS[i].filePath;
        const char* data = BUNDLED_APP_SCRIPTS[i].progmemData;
        writeProgmemFile(path, data);
    }
}

bool StorageHAL::fileExists(const char* path) {
    if (!path) return false;
    if (isSDPath(path) && !s_sdMounted) return false;
    fs::FS &fs = getFSForPath(path);
    const char* target = isSDPath(path) ? stripSDPrefix(path) : path;
    return fs.exists(target);
}

size_t StorageHAL::getFileSize(const char* path) {
    if (!path) return 0;
    if (isSDPath(path) && !s_sdMounted) return 0;
    fs::FS &fs = getFSForPath(path);
    const char* target = isSDPath(path) ? stripSDPrefix(path) : path;

    File f = fs.open(target, "r");
    if (!f) return 0;
    size_t sz = f.size();
    f.close();
    return sz;
}

bool StorageHAL::readFile(const char* path, String &outContent) {
    outContent = "";
    if (!path) return false;
    if (isSDPath(path) && !s_sdMounted) return false;

    fs::FS &fs = getFSForPath(path);
    const char* target = isSDPath(path) ? stripSDPrefix(path) : path;

    File f = fs.open(target, "r");
    if (!f || f.isDirectory()) {
        return false;
    }

    size_t sz = f.size();
    // Safety check: protect internal RAM from out-of-memory if huge file is requested
    if (sz > 65536) {
        Serial.printf("[StorageHAL] WARNING: File %s exceeds 64KB read buffer safety limit!\n", path);
    }

    // Reserve string buffer to eliminate frequent heap reallocations
    outContent.reserve(sz + 1);

    char chunk[256];
    while (f.available()) {
        size_t bytesRead = f.readBytes(chunk, sizeof(chunk) - 1);
        if (bytesRead > 0) {
            chunk[bytesRead] = '\0';
            outContent += chunk;
        }
    }

    f.close();
    return true;
}

bool StorageHAL::writeFile(const char* path, const char* content) {
    if (!path || !content) return false;
    return writeFile(path, (const uint8_t*)content, strlen(content));
}

bool StorageHAL::writeFile(const char* path, const uint8_t* buffer, size_t length) {
    if (!path) return false;
    if (isSDPath(path) && !s_sdMounted) return false;

    // Auto-create parent directory if path contains slashes
    String pathStr(path);
    int lastSlash = pathStr.lastIndexOf('/');
    if (lastSlash > 0) {
        String dir = pathStr.substring(0, lastSlash);
        ensureDirectory(dir.c_str());
    }

    fs::FS &fs = getFSForPath(path);
    const char* target = isSDPath(path) ? stripSDPrefix(path) : path;

    File f = fs.open(target, "w");
    if (!f) {
        Serial.printf("[StorageHAL] Failed to open %s for write.\n", path);
        return false;
    }

    size_t written = 0;
    if (buffer && length > 0) {
        written = f.write(buffer, length);
    }
    f.flush();
    f.close();

    return (written == length);
}

bool StorageHAL::appendFile(const char* path, const char* content) {
    if (!path || !content) return false;
    if (isSDPath(path) && !s_sdMounted) return false;

    fs::FS &fs = getFSForPath(path);
    const char* target = isSDPath(path) ? stripSDPrefix(path) : path;

    File f = fs.open(target, "a");
    if (!f) return false;

    size_t len = strlen(content);
    size_t written = f.write((const uint8_t*)content, len);
    f.flush();
    f.close();

    return (written == len);
}

bool StorageHAL::deleteFile(const char* path) {
    if (!path) return false;
    if (isSDPath(path) && !s_sdMounted) return false;
    fs::FS &fs = getFSForPath(path);
    const char* target = isSDPath(path) ? stripSDPrefix(path) : path;
    return fs.remove(target);
}

bool StorageHAL::hasExtension(const char* filename, const char* ext) {
    if (!filename || !ext) return false;
    size_t fnLen = strlen(filename);
    size_t extLen = strlen(ext);
    if (fnLen < extLen) return false;
    return (strcasecmp(filename + fnLen - extLen, ext) == 0);
}

bool StorageHAL::listDirectory(const char* dirPath, std::vector<String> &outFiles, const char* extensionFilter) {
    outFiles.clear();
    if (!dirPath) return false;
    if (isSDPath(dirPath) && !s_sdMounted) return false;

    fs::FS &fs = getFSForPath(dirPath);
    const char* target = isSDPath(dirPath) ? stripSDPrefix(dirPath) : dirPath;

    File dir = fs.open(target);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return false;
    }

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            const char* entryName = entry.name();
            if (entryName && entryName[0] != '\0') {
                String en(entryName);
                int lastSlash = en.lastIndexOf('/');
                String baseName = (lastSlash >= 0) ? en.substring(lastSlash + 1) : en;

                // Ignore hidden system files (._* or .Trashes)
                if (baseName.length() > 0 && !baseName.startsWith(".")) {
                    if (extensionFilter == nullptr || hasExtension(baseName.c_str(), extensionFilter)) {
                        String fullPath(dirPath);
                        if (!fullPath.endsWith("/")) fullPath += "/";
                        fullPath += baseName;
                        outFiles.push_back(fullPath);
                    }
                }
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return true;
}

bool StorageHAL::isSDPath(const char* path) {
    if (!path) return false;
    return (strncmp(path, "/sd/", 4) == 0 || strcmp(path, "/sd") == 0);
}

const char* StorageHAL::stripSDPrefix(const char* path) {
    if (!path) return "";
    if (strncmp(path, "/sd/", 4) == 0) {
        return path + 3; // e.g. "/sd/apps/demo.lua" -> "/apps/demo.lua"
    }
    if (strcmp(path, "/sd") == 0) {
        return "/";
    }
    return path;
}

fs::FS& StorageHAL::getFSForPath(const char* path) {
    if (isSDPath(path) && s_sdMounted) {
        return SD;
    }
    return LittleFS;
}
