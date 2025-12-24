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

struct FWRelease { std::string name; std::string url; };
FWRelease fw_list[30]; 
int total_found = 0;

void deleteRecursive(const char* path) {
    DIR* d = opendir(path);
    if (!d) return;
    struct dirent* p;
    while ((p = readdir(d))) {
        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s/%s", path, p->d_name);
        struct stat st;
        if (!stat(buf, &st)) {
            if (S_ISDIR(st.st_mode)) deleteRecursive(buf);
            else unlink(buf);
        }
    }
    closedir(d); rmdir(path);
}

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

int progress_callback(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ult, curl_off_t uln) {
    if (dltotal > 0) {
        float percentage = (float)dlnow / dltotal * 100.0f;
        printf("\r\x1b[K\x1b[32m[%s] Dang tai: %.1f%%\x1b[0m", (char*)p, percentage);
        consoleUpdate(NULL);
    }
    return 0;
}

void decompressAndDistribute(const char* zipPath, bool isMyPack, const char* targetRoot) {
    unzFile uf = unzOpen(zipPath);
    if (!uf) return;
    unz_global_info gi; unzGetGlobalInfo(uf, &gi);
    
    if(isMyPack) { deleteRecursive(STAGING_DIR); mkdir(STAGING_DIR, 0777); }

    for (uLong i = 0; i < gi.number_entry; ++i) {
        char filename[256];
        unzGetCurrentFileInfo(uf, NULL, filename, sizeof(filename), NULL, 0, NULL, 0);
        char targetPath[1024];
        
        if (isMyPack && (strstr(filename, "package3") || strstr(filename, "stratosphere.romfs")))
            snprintf(targetPath, 1024, "%s/%s", STAGING_DIR, filename);
        else
            snprintf(targetPath, 1024, "%s/%s", targetRoot, filename);

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
}

void fetchGitHubReleases(const char* repo) {
    CURL *curl = curl_easy_init();
    std::string readBuffer; total_found = 0;
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "User-Agent: DracoUpdater");
        curl_easy_setopt(curl, CURLOPT_URL, (std::string("https://api.github.com/repos/") + repo + "/releases").c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        if(curl_easy_perform(curl) == CURLE_OK) {
            size_t pos = 0;
            for (int i = 0; i < 20; i++) {
                pos = readBuffer.find("\"tag_name\":\"", pos);
                if (pos == std::string::npos) break;
                size_t n_start = pos + 12; size_t n_end = readBuffer.find("\"", n_start);
                fw_list[i].name = readBuffer.substr(n_start, n_end - n_start);
                
                size_t asset_pos = readBuffer.find("\"browser_download_url\":\"", n_end);
                while(asset_pos != std::string::npos) {
                    size_t u_start = asset_pos + 24; size_t u_end = readBuffer.find("\"", u_start);
                    std::string url = readBuffer.substr(u_start, u_end - u_start);
                    if(url.find(".zip") != std::string::npos) { fw_list[i].url = url; break; }
                    asset_pos = readBuffer.find("\"browser_download_url\":\"", u_end);
                    if (asset_pos > readBuffer.find("}", u_end)) break; 
                }
                total_found++; pos = n_end;
            }
        }
        curl_easy_cleanup(curl);
    }
}

std::string getLatestZip(const char* repo) {
    CURL *curl = curl_easy_init();
    std::string readBuffer; std::string res = "";
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "User-Agent: DracoUpdater");
        curl_easy_setopt(curl, CURLOPT_URL, (std::string("https://api.github.com/repos/") + repo + "/releases/latest").c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        if(curl_easy_perform(curl) == CURLE_OK) {
            size_t pos = readBuffer.find("\"browser_download_url\":\"");
            while(pos != std::string::npos) {
                size_t u_start = pos + 24; size_t u_end = readBuffer.find("\"", u_start);
                std::string url = readBuffer.substr(u_start, u_end - u_start);
                if(url.find(".zip") != std::string::npos) { res = url; break; }
                pos = readBuffer.find("\"browser_download_url\":\"", u_end);
            }
        }
        curl_easy_cleanup(curl);
    }
    return res;
}

int showSelectionMenu(const char* title, PadState* pad) {
    int sel = 0;
    // Thêm lệnh xóa màn hình ngay khi vừa vào menu chọn
    printf("\x1b[2J\x1b[H"); 
    consoleClear();

    while(appletMainLoop()) {
        // Đưa con trỏ về đầu hàng 1
        printf("\x1b[1;1H"); 
        
        // In tiêu đề menu chọn (đã được làm sạch)
        printf("\x1b[36m=== %s ===\x1b[0m\n", title);
        printf("\x1b[33mPowered by Draco\x1b[0m\n\n");

        for(int i=0; i<total_found; i++) {
            // Thêm \x1b[K để xóa trắng phần thừa phía sau mỗi dòng
            printf("%s %-35s\x1b[K\n", (i==sel)?"\x1b[32m >":"   ", fw_list[i].name.c_str());
        }

        // Xóa trắng toàn bộ vùng còn lại bên dưới danh sách để không bị dính chữ cũ
        printf("\x1b[J"); 
        
        printf("\n(A) Tai ve | (B) Quay lai\x1b[K");
        
        padUpdate(pad); u64 k = padGetButtonsDown(pad);
        if(k & HidNpadButton_AnyUp) sel = (sel-1+total_found)%total_found;
        if(k & HidNpadButton_AnyDown) sel = (sel+1)%total_found;
        if(k & HidNpadButton_A) return sel;
        if(k & HidNpadButton_B) { 
            // Khi thoát menu chọn cũng phải xóa sạch để menu chính hiện ra đẹp
            printf("\x1b[2J\x1b[H"); 
            consoleClear(); 
            return -1; 
        }
        consoleUpdate(NULL);
    }
    return -1;
}

int main(int argc, char** argv) {
    consoleInit(NULL);
    socketInitializeDefault();
    PadState pad; padConfigureInput(1, HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);

    bool exitApp = false;
    while (appletMainLoop() && !exitApp) {
        int mainSelection = 0; int modeChosen = -1; bool taskDone = false;

        while (appletMainLoop()) {
            printf("\x1b[1;1H\x1b[36m");
            printf(" __  __  __     __  _____   ___    ___  _  __\n");
            printf("|  \\/  | \\ \\   / / |  __ \\ / _ \\  / __|| |/ /\n");
            printf("| |\\/| |  \\ \\_/ /  | |__) | |_| || |   | ' / \n");
            printf("| |  | |   \\   /   |  ___/|  _  || |   |  <  \n");
            printf("| |  | |    | |    | |    | | | || |__ | . \\ \n");
            printf("|_|  |_|    |_|    |_|    |_| |_| \\___||_|\\_\\\n");
            printf("\x1b[0m\x1b[33m=============================================\x1b[0m\n");
            printf("\x1b[35m              BY DRACO v1.0\x1b[0m\n\n");
            
            printf("%s [1] CAP NHAT MY PACK (Ban day du moi nhat) %-5s\n", (mainSelection == 0) ? "\x1b[32m >" : "   ", "");
            printf("%s [2] CAP NHAT ATMOSPHERE (Chon ban) %-5s\n", (mainSelection == 1) ? "\x1b[32m >" : "   ", "");
            printf("%s [3] CAP NHAT HEKATE (moi nhat) %-5s\n", (mainSelection == 2) ? "\x1b[32m >" : "   ", "");
            printf("%s [4] CAP NHAT SYS-PATCH (moi nhat) %-5s\n", (mainSelection == 3) ? "\x1b[32m >" : "   ", "");
            printf("%s [5] TAI FIRMWARE MOI (Chon ban) %-5s\n", (mainSelection == 4) ? "\x1b[32m >" : "   ", "");
            printf("%s [6] THOAT %-5s\n", (mainSelection == 5) ? "\x1b[31m >" : "   ", "");
            
            printf("\x1b[J\n(D-Pad) Di chuyen | (A) Xac nhan\x1b[K");

            padUpdate(&pad); u64 k = padGetButtonsDown(&pad);
            if (k & HidNpadButton_AnyUp) mainSelection = (mainSelection-1+6)%6;
            if (k & HidNpadButton_AnyDown) mainSelection = (mainSelection+1)%6;
            if (k & HidNpadButton_A) { if(mainSelection==5) exitApp=true; else modeChosen=mainSelection; break; }
            consoleUpdate(NULL);
        }

        if (exitApp) break;
        consoleClear();
        std::string label = ""; // Fix lỗi Scope ở đây
        CURL *curl = curl_easy_init();
        if (curl) {
            std::string url = ""; std::string label = ""; bool isFW = false;

            if (modeChosen == 0) { url = DOWNLOAD_URL; label = "MY PACK"; }
            else if (modeChosen == 1) { 
                printf("Dang lay danh sach Atmosphere...\n"); fetchGitHubReleases("atmosphere-nx/atmosphere");
                int s = showSelectionMenu("CHON ATMOSPHERE", &pad);
                if(s >= 0) { url = fw_list[s].url; label = "ATMOSPHERE"; }
            }
            else if (modeChosen == 2) { url = getLatestZip("ctcaer/hekate"); label = "HEKATE"; }
            else if (modeChosen == 3) { url = getLatestZip("impeeza/sys-patch"); label = "SYS-PATCH"; }
            else if (modeChosen == 4) {
                printf("Dang lay danh sach Firmware...\n"); fetchGitHubReleases("THZoria/NX_Firmware");
                int s = showSelectionMenu("CHON FIRMWARE", &pad);
                if(s >= 0) { url = fw_list[s].url; label = fw_list[s].name; isFW = true; }
            }

            if (!url.empty()) {
                consoleClear();
                FILE *fp = fopen(isFW ? FW_ZIP_PATH : ZIP_PATH, "wb");
                if (fp) {
                    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
                    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
                    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void*)label.c_str());
                    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                    if (curl_easy_perform(curl) == CURLE_OK) {
                        fclose(fp);
                        if(isFW) { decompressAndDistribute(FW_ZIP_PATH, false, FW_OUT_DIR); unlink(FW_ZIP_PATH); }
                        else {
                            if(modeChosen == 1) { printf("Cleaning /atmosphere/contents...\n"); deleteRecursive("sdmc:/atmosphere/contents"); }
                            decompressAndDistribute(ZIP_PATH, (modeChosen == 0), "sdmc:");
                            if(modeChosen == 0) {
                                const char* mods[] = {"010000000000bd00", "0100000000000352", "0000000000534c54"};
                                for(int i=0; i<3; i++) {
                                    char p[256]; snprintf(p, 256, "sdmc:/atmosphere/contents/%s", mods[i]);
                                    deleteRecursive(p);
                                }
                                mkdir("sdmc:/tegraexplorer/scripts", 0777);
                                FILE* f = fopen("sdmc:/tegraexplorer/scripts/autorun.te", "w");
                                fprintf(f, "clear()\ncopyfile(\"sd:/update_staging/atmosphere/package3\", \"sd:/atmosphere/package3\")\ncopyfile(\"sd:/update_staging/atmosphere/stratosphere.romfs\", \"sd:/atmosphere/stratosphere.romfs\")\ndeldir(\"sd:/update_staging\")\nprintln(\"SUCCESS!\")\ndelfile(\"sd:/tegraexplorer/scripts/autorun.te\")\npause()\n");
                                fclose(f);
                            }
                            unlink(ZIP_PATH);
                        }
                        taskDone = true;
                    } else { fclose(fp); printf("Loi tai file!\n"); }
                }
            }
            curl_easy_cleanup(curl);
        }

       // --- MÀN HÌNH KẾT THÚC (LOG RIÊNG CHO TỪNG MODE) ---
        if (taskDone) {
            printf("\n\x1b[32m[THANH CONG] %s DA SAN SANG!\x1b[0m\n", label.c_str());
            printf("\x1b[33m------------------------------------------\n");

            switch (modeChosen) {
                case 0: // My Pack
                    printf("Nhiem vu: CAP NHAT MY PACK\n");
                    printf(">> Bam (+) de Reboot vao Hekate\n");
                    printf(">> Vao Payloads -> TegraExplorer.bin\n");
                    printf(">> Chon 'autorun.te' de hoan tat.\n");
                    break;
                case 1: // Atmosphere
                    printf(">> Bam (+) de Reboot vao Hekate\n");
                    printf(">> Vao Payloads -> TegraExplorer.bin\n");
                    printf(">> Chon 'autorun.te' de hoan tat.\n");
                    break;
                case 2: // Hekate
                    printf("Nhiem vu: CAP NHAT HEKATE\n");
                    printf(">> Bam (+) de Reboot vao Hekate\n");
                    printf(">> Bam Refresh de Update\n");
                    break;
                case 3: // Sys-patch
                    printf("Nhiem vu: CAP NHAT SYS-PATCH\n");
                     printf(">> Bam (+) de Reboot vao Hekate\n");
                    printf(">> Bam Refresh de Update\n");
                    break;
                case 4: // Firmware
                    printf("Nhiem vu: TAI FIRMWARE MOI\n");
                    printf(">> Da giai nen vao folder /FW_Update.\n");
                    printf(">> Mo Album -> Daybreak de cai dat.\n");
                    break;
            }
            printf("------------------------------------------\x1b[0m\n");
            printf("\x1b[35mPowered by Draco - Tinfoil Hac Am Group\x1b[0m\n");
        }

        printf("\x1b[36m(B) Quay lai Menu | (+) Thoat/Reboot\x1b[0m\n");

       while (appletMainLoop()) {
            padUpdate(&pad); 
            u64 kDown = padGetButtonsDown(&pad);

            if (kDown & HidNpadButton_B) { 
                printf("\x1b[2J\x1b[H"); 
                consoleClear(); 
                break; 
            }

            if (kDown & HidNpadButton_Plus) { 
                if (taskDone) {
                    if (modeChosen >= 0 && modeChosen <= 3) {
                        printf("\n\x1b[33mDang khoi dong lai vao Hekate...\x1b[0m\n");
                        consoleUpdate(NULL);
                        spsmInitialize(); 
                        spsmShutdown(true); // Reboot
                    }
                    else if (modeChosen == 4) {
                        exitApp = true;
                        break;
                    }
                } else {
                    // Nếu chưa làm gì mà bấm (+) thì cứ thoát app
                    exitApp = true;
                    break;
                }
            }
            consoleUpdate(NULL);
        }
    }
    socketExit(); consoleExit(NULL); return 0;
}