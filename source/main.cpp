#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <switch.h>
#include <curl/curl.h>
#include <minizip/unzip.h>
#include <string>
#include <vector>

const char* DOWNLOAD_URL = "https://www.dropbox.com/scl/fi/9vza0a3ubr00cyq209hks/My-pack.zip?rlkey=hhv87nxuqy1izx21h9hqel40s&dl=1";
const char* ZIP_PATH     = "sdmc:/update_temp.zip";
const char* STAGING_DIR  = "sdmc:/update_staging";
const char* FW_ZIP_PATH  = "sdmc:/fw_temp.zip";
const char* FW_OUT_DIR   = "sdmc:/FW_Update";

struct FWRelease {
    std::string name;
    std::string url;
};
FWRelease fw_list[30];
int total_fw_found = 0;
char current_fw_name[256] = "Firmware.zip";

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

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

int progress_callback(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ult, curl_off_t uln) {
    if (dltotal > 0) {
        float percentage = (float)dlnow / dltotal * 100.0f;
        printf("\r\x1b[K\x1b[32m[%s] %.1f%%\x1b[0m", (char*)p, percentage);
        consoleUpdate(NULL);
    }
    return 0;
}

void decompressAndDistribute(const char* zipPath) {
    unzFile uf = unzOpen(zipPath);
    if (!uf) return;
    unz_global_info gi;
    unzGetGlobalInfo(uf, &gi);
    deleteRecursive(STAGING_DIR);
    mkdir(STAGING_DIR, 0777);
    for (uLong i = 0; i < gi.number_entry; ++i) {
        char filename[256];
        unzGetCurrentFileInfo(uf, NULL, filename, sizeof(filename), NULL, 0, NULL, 0);
        char targetPath[1024];
        if (strstr(filename, "package3") || strstr(filename, "stratosphere.romfs"))
            snprintf(targetPath, 1024, "%s/%s", STAGING_DIR, filename);
        else
            snprintf(targetPath, 1024, "sdmc:/%s", filename);

        printf("\x1b[32m[%3.1f%%]\x1b[0m Dang chep: %s\n", (float)(i+1)/gi.number_entry*100, filename);
        consoleUpdate(NULL);

        if (filename[strlen(filename)-1] == '/') mkdir(targetPath, 0777);
        else {
            char dir[1024]; strcpy(dir, targetPath); char* p = strrchr(dir, '/');
            if (p) { *p = 0; mkdir(dir, 0777); }
            if (unzOpenCurrentFile(uf) == UNZ_OK) {
                FILE* out = fopen(targetPath, "wb");
                if (out) {
                    char buf[65536]; int n;
                    while ((n = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0) fwrite(buf, n, 1, out);
                    fclose(out);
                }
                unzCloseCurrentFile(uf);
            }
        }
        unzGoToNextFile(uf);
    }
    unzClose(uf);
    printf("\n\x1b[36m>>> Giai nen xong Pack!\x1b[0m\n");
}

void decompressFW(const char* zipPath) {
    printf("\x1b[33mDang don dep folder Firmware cu...\x1b[0m\n");
    deleteRecursive(FW_OUT_DIR); 
    mkdir(FW_OUT_DIR, 0777);

    unzFile uf = unzOpen(zipPath);
    if (!uf) {
        printf("\x1b[31m[!] Khong the mo file ZIP Firmware!\x1b[0m\n");
        return;
    }

    unz_global_info gi;
    unzGetGlobalInfo(uf, &gi);
    
    for (uLong i = 0; i < gi.number_entry; ++i) {
        char filename[256];
        unzGetCurrentFileInfo(uf, NULL, filename, sizeof(filename), NULL, 0, NULL, 0);
        char targetPath[1024];
        snprintf(targetPath, 1024, "%s/%s", FW_OUT_DIR, filename);
        
        printf("\x1b[33m[%3.1f%%]\x1b[0m Dang chep FW: %s\n", (float)(i+1)/gi.number_entry*100, filename);
        consoleUpdate(NULL);
        
        if (filename[strlen(filename)-1] == '/') {
            mkdir(targetPath, 0777);
        } else {
            char dir[1024]; 
            strcpy(dir, targetPath); 
            char* p = strrchr(dir, '/');
            if (p) { *p = 0; mkdir(dir, 0777); }

            if (unzOpenCurrentFile(uf) == UNZ_OK) {
                FILE* out = fopen(targetPath, "wb");
                if (out) {
                    char buf[65536]; 
                    int n;
                    while ((n = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0) {
                        fwrite(buf, n, 1, out);
                    }
                    fclose(out);
                }
                unzCloseCurrentFile(uf);
            }
        }
        unzGoToNextFile(uf);
    }
    unzClose(uf);
    printf("\n\x1b[36m>>> Giai nen xong Firmware!\x1b[0m\n");
}

void fetchFWList() {
    CURL *curl = curl_easy_init();
    std::string readBuffer;
    total_fw_found = 0;
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "User-Agent: Switch-Updater-Pro");
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/THZoria/NX_Firmware/releases");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        if(curl_easy_perform(curl) == CURLE_OK) {
            size_t pos = 0;
            for (int i = 0; i < 30; i++) {
                pos = readBuffer.find("\"tag_name\":\"");
                if (pos == std::string::npos) break;
                size_t n_start = pos + 12;
                size_t n_end = readBuffer.find("\"", n_start);
                fw_list[i].name = readBuffer.substr(n_start, n_end - n_start);
                pos = readBuffer.find("\"browser_download_url\":\"", n_end);
                if (pos == std::string::npos) break;
                size_t u_start = pos + 24;
                size_t u_end = readBuffer.find("\"", u_start);
                fw_list[i].url = readBuffer.substr(u_start, u_end - u_start);
                total_fw_found++;
                readBuffer.erase(0, u_end); 
            }
        }
        curl_easy_cleanup(curl);
    }
}

void createTegraScript() {
    mkdir("sdmc:/tegraexplorer/scripts", 0777);
    FILE* f = fopen("sdmc:/tegraexplorer/scripts/autorun.te", "w");
    if (f) {
        fprintf(f, "clear()\nprintln(\"Tien hanh cai dat update...\")\n");
        fprintf(f, "copyfile(\"sd:/update_staging/atmosphere/package3\", \"sd:/atmosphere/package3\")\n");
        fprintf(f, "copyfile(\"sd:/update_staging/atmosphere/stratosphere.romfs\", \"sd:/atmosphere/stratosphere.romfs\")\n");
        fprintf(f, "deldir(\"sd:/update_staging\")\ndelfile(\"sd:/tegraexplorer/scripts/autorun.te\")\npause()\n");
        fclose(f);
    }
}

int main(int argc, char** argv) {
    consoleInit(NULL);
    socketInitializeDefault();
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    bool exitApp = false;

    while (appletMainLoop() && !exitApp) {
        int mainSelection = 0;
        int modeChosen = -1;
        bool taskDone = false;

        while (appletMainLoop()) {
            printf("\x1b[1;1H\x1b[36m=== DRACO ULTIMATE UPDATER ===\x1b[0m\n\n");
            printf("Chon nhiem vu ban muon thuc hien:\n\n");
            printf("%s [1] CAP NHAT MY PACK (Atmosphere)\x1b[0m\n", (mainSelection == 0) ? "\x1b[32m >" : "   ");
            printf("%s [2] TAI FIRMWARE MOI (Daybreak)\x1b[0m\n", (mainSelection == 1) ? "\x1b[32m >" : "   ");
            printf("%s [3] THOAT\x1b[0m\n", (mainSelection == 2) ? "\x1b[31m >" : "   ");
            printf("\n(D-Pad) Di chuyen | (A) Xac nhan\x1b[K\n");

            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);
            if (kDown & HidNpadButton_AnyUp) mainSelection = (mainSelection - 1 + 3) % 3;
            if (kDown & HidNpadButton_AnyDown) mainSelection = (mainSelection + 1) % 3;
            if (kDown & HidNpadButton_A) {
                if (mainSelection == 2) { exitApp = true; break; }
                modeChosen = mainSelection;
                break;
            }
            consoleUpdate(NULL);
        }

        if (exitApp) break;
        consoleClear();
        CURL *curl = curl_easy_init();

        if (curl) {
            if (modeChosen == 0) { 
                FILE *fp = fopen(ZIP_PATH, "wb");
                if (fp) {
                    printf("\x1b[33mDang tai My Pack...\x1b[0m\n");
                    curl_easy_setopt(curl, CURLOPT_URL, DOWNLOAD_URL);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
                    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
                    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void*)"PACK");
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
                        taskDone = true;
                    } else { fclose(fp); printf("\x1b[31mLoi tai Pack!\x1b[0m\n"); }
                }
            } else if (modeChosen == 1) { 
                printf("Dang lay danh sach tu GitHub...\n");
                fetchFWList();
                int fwSelected = 0; bool confirmedFW = false;
                if (total_fw_found == 0) {
                    printf("\x1b[31mKhong the ket noi GitHub!\x1b[0m\n");
                } else {
                    while(appletMainLoop()) {
                        printf("\x1b[1;1H\x1b[36m=== CHON PHIEN BAN FIRMWARE ===\x1b[0m\n\n");
                        for(int i = 0; i < total_fw_found; i++)
                            printf("%s %-35s\x1b[0m\n", (i == fwSelected) ? "\x1b[32m >" : "   ", fw_list[i].name.c_str());
                        printf("\n(A) Tai ve | (B) Quay lai\x1b[K\n");
                        
                        padUpdate(&pad); u64 fwDown = padGetButtonsDown(&pad);
                        if (fwDown & HidNpadButton_AnyUp) fwSelected = (fwSelected - 1 + total_fw_found) % total_fw_found;
                        if (fwDown & HidNpadButton_AnyDown) fwSelected = (fwSelected + 1) % total_fw_found;
                        if (fwDown & HidNpadButton_A) { confirmedFW = true; break; }
                        if (fwDown & HidNpadButton_B) break;
                        consoleUpdate(NULL);
                    }
                    if (confirmedFW) {
                        consoleClear();
                        printf("\x1b[32m[OK] Bat dau xu ly ban: %s\x1b[0m\n", fw_list[fwSelected].name.c_str());
                        deleteRecursive(FW_OUT_DIR); mkdir(FW_OUT_DIR, 0777);
                        FILE *fwFp = fopen(FW_ZIP_PATH, "wb");
                        if (fwFp) {
                            curl_easy_setopt(curl, CURLOPT_URL, fw_list[fwSelected].url.c_str());
                            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fwFp);
                            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void*)fw_list[fwSelected].name.c_str());
                            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
                            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                            if (curl_easy_perform(curl) == CURLE_OK) {
                                fclose(fwFp);
                                decompressFW(FW_ZIP_PATH);
                                unlink(FW_ZIP_PATH);
                                taskDone = true;
                            } else { fclose(fwFp); printf("\x1b[31mLoi tai FW!\x1b[0m\n"); }
                        }
                    }
                }
            }
            curl_easy_cleanup(curl);
        }

        if (taskDone) {
            printf("\n\x1b[32m[THANH CONG] Moi thu da san sang!\x1b[0m\n");
            printf("\x1b[33m------------------------------------------\n");
            if (modeChosen == 0) {
                printf("Nhiem vu: CAP NHAT MY PACK\n");
                printf("1. Bam (+) de Reboot vao Hekate\n");
                printf("2. Chon 'Payloads' -> 'TegraExplorer.bin'\n");
                printf("3. Chon 'autorun.te' de hoan tat\n");
            } else {
                printf("Nhiem vu: TAI FIRMWARE MOI\n");
                printf("1. Bam (+) de thoat ra Home Menu\n");
                printf("2. Mo album -> Chon 'Daybreak'\n");
                printf("3. Chon folder '/FW_Update' de cai dat\n");
            }
            printf("------------------------------------------\x1b[0m\n");
        }
        
        printf("\x1b[36m(B) Quay lai Menu | (+) Reboot/Thoat\x1b[0m\n");

        while (appletMainLoop()) {
            padUpdate(&pad); u64 kDown = padGetButtonsDown(&pad);
            if (kDown & HidNpadButton_B) { consoleClear(); break; }
            if (kDown & HidNpadButton_Plus) {
                if (modeChosen == 0 && taskDone) { spsmInitialize(); spsmShutdown(true); }
                exitApp = true; break;
            }
            consoleUpdate(NULL);
        }
    }
    socketExit(); consoleExit(NULL); return 0;
}