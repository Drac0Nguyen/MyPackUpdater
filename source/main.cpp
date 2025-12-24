#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <switch.h>
#include <curl/curl.h>
#include <minizip/unzip.h>

const char* DOWNLOAD_URL = "https://www.dropbox.com/scl/fi/9vza0a3ubr00cyq209hks/My-pack.zip?rlkey=hhv87nxuqy1izx21h9hqel40s&dl=1"; 
const char* ZIP_PATH     = "sdmc:/update_temp.zip";
const char* STAGING_DIR  = "sdmc:/update_staging";

void deleteRecursive(const char* path) {
    DIR* d = opendir(path);
    if (!d) return;
    struct dirent* p;
    while ((p = readdir(d))) {
        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s/%s", path, p->d_name);
        struct stat statbuf;
        if (!stat(buf, &statbuf)) {
            if (S_ISDIR(statbuf.st_mode)) deleteRecursive(buf);
            else unlink(buf);
        }
    }
    closedir(d);
    rmdir(path);
}

int progress_callback(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ult, curl_off_t uln) {
    if (dltotal > 0) {
        float percentage = (float)dlnow / dltotal * 100.0f;
        printf("\r\x1b[K\x1b[32m[TAI FILE] %.1f%%\x1b[0m", percentage);
        consoleUpdate(NULL);
    }
    return 0;
}

void decompressAndDistribute(const char* zipPath) {
    unzFile uf = unzOpen(zipPath);
    if (!uf) return;
    
    unz_global_info gi;
    unzGetGlobalInfo(uf, &gi);
    uLong totalFiles = gi.number_entry;
    char buf[65536];

    deleteRecursive(STAGING_DIR);
    mkdir(STAGING_DIR, 0777);

    for (uLong i = 0; i < totalFiles; ++i) {
        char filename[256];
        unzGetCurrentFileInfo(uf, NULL, filename, sizeof(filename), NULL, 0, NULL, 0);
        
        float percentage = (float)(i + 1) / totalFiles * 100.0f;
        char targetPath[1024];

        if (strstr(filename, "package3") || strstr(filename, "stratosphere.romfs")) {
            snprintf(targetPath, 1024, "%s/%s", STAGING_DIR, filename);
        } else {
            snprintf(targetPath, 1024, "sdmc:/%s", filename);
        }

        printf("\r\x1b[K\x1b[32m[CHEP %3.1f%%]\x1b[0m %s", percentage, filename);
        consoleUpdate(NULL);

        if (filename[strlen(filename) - 1] == '/') {
            mkdir(targetPath, 0777);
        } else {
            char dir[1024]; strcpy(dir, targetPath); char* p = strrchr(dir, '/');
            if (p) { *p = 0; mkdir(dir, 0777); }
            if (unzOpenCurrentFile(uf) == UNZ_OK) {
                FILE* out = fopen(targetPath, "wb");
                if (out) {
                    int n;
                    while ((n = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0) fwrite(buf, n, 1, out);
                    fclose(out);
                }
                unzCloseCurrentFile(uf);
            }
        }
        unzGoToNextFile(uf);
    }
    unzClose(uf);
    printf("\nGiai nen xong!\n");
}

void createTegraScript() {
    mkdir("sdmc:/tegraexplorer/scripts", 0777);
    FILE* f = fopen("sdmc:/tegraexplorer/scripts/autorun.te", "w");
    if (f) {
        fprintf(f, "clear()\n");
        fprintf(f, "println(\"Tien hanh cai dat update...\")\n");
        fprintf(f, "copyfile(\"sd:/update_staging/atmosphere/package3\", \"sd:/atmosphere/package3\")\n");
        fprintf(f, "copyfile(\"sd:/update_staging/atmosphere/stratosphere.romfs\", \"sd:/atmosphere/stratosphere.romfs\")\n");
        fprintf(f, "deldir(\"sd:/update_staging\")\n");
        fprintf(f, "delfile(\"sd:/tegraexplorer/scripts/autorun.te\")\n");
        fprintf(f, "println(\"----------------------------------------------------------------\")\n");
        fprintf(f, "println(\"Cai dat thanh cong\")\n");
        fprintf(f, "println(\"Bam nut bat ky de tro ve\")\n");
        fprintf(f, "println(\"Chon reboot update.bin de tro ve Hentake de thuong thuc\")\n");
        fprintf(f, "println(\"----------------------------------------------------------------\")\n");
        fprintf(f, "pause()\n");
        fclose(f);
    }
}

int main(int argc, char** argv) {
    consoleInit(NULL);
    socketInitializeDefault();

    printf("\x1b[36m=== MY PACK UPDATER BY DRACO ===\x1b[0m\n\n");

    CURL *curl = curl_easy_init();
    if (curl) {
        FILE *fp = fopen(ZIP_PATH, "wb");
        if (fp) {
            curl_easy_setopt(curl, CURLOPT_URL, DOWNLOAD_URL);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            if (curl_easy_perform(curl) == CURLE_OK) {
                fclose(fp);
                
                decompressAndDistribute(ZIP_PATH);

                printf("Cleaning sysmodules...\n");
                const char* mods[] = {"010000000000bd00", "0100000000000352", "0000000000534c54"};
                for(int i=0; i<3; i++) {
                    char p[256]; snprintf(p, 256, "sdmc:/atmosphere/contents/%s", mods[i]);
                    deleteRecursive(p);
                }
                createTegraScript();
                unlink(ZIP_PATH);
                printf("\n\x1b[32m[THANH CONG] Moi thu da san sang!\x1b[0m\n");
                printf("\x1b[33m------------------------------------------\x1b[0m\n");
                printf("1. Bam (+) de Reboot vao Hekate\n");
                printf("2. Chon 'Payloads' -> 'TegraExplorer.bin'.\n");
                printf("3. Chon 'autorun.te'.\n");
                printf("\x1b[33m------------------------------------------\x1b[0m\n");
            } else {
                printf("\x1b[31mDOWNLOAD ERROR!\x1b[0m\n");
            }
        }
        curl_easy_cleanup(curl);
    }

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while(appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            spsmInitialize();
            spsmShutdown(true); 
            break;
        }
        consoleUpdate(NULL);
    }
    socketExit();
    consoleExit(NULL);
    return 0;
}