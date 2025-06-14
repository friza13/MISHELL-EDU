#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 100
#define MAX_HISTORY 10
#define MAX_API_KEY_LEN 100
#define MAX_RESPONSE_SIZE 65536
#define API_KEY_FILE ".mishell_api_key"

// Prototype/deklarasi fungsi-fungsi
char* remove_surrounding_quotes(char* str);
char* escape_path(const char* path);
void replace_html_escapes(char* str);
void check_cpu();
void check_ram();
void check_disk();

// Menyimpan sejarah perintah yang dijalankan
char history[MAX_HISTORY][MAX_CMD_LEN];
int history_count = 0;

// Variabel untuk menyimpan Gemini API key
char gemini_api_key[MAX_API_KEY_LEN] = "";
int is_api_key_set = 0;

// Struktur data untuk menyimpan respons dari API
struct MemoryStruct {
    char *memory;
    size_t size;
};

// Fungsi untuk mendapatkan path file API key
void get_api_key_path(char *path, size_t size) {
    char *home_dir = getenv("HOME");
    if (home_dir != NULL) {
        snprintf(path, size, "%s/%s", home_dir, API_KEY_FILE);
    } else {
        snprintf(path, size, "./%s", API_KEY_FILE);
    }
}

// Fungsi untuk menyimpan API key ke file
void save_api_key() {
    if (!is_api_key_set) return;
    
    char api_key_path[MAX_CMD_LEN];
    get_api_key_path(api_key_path, sizeof(api_key_path));
    
    FILE *file = fopen(api_key_path, "w");
    if (file == NULL) {
        fprintf(stderr, "Error: Tidak dapat menyimpan API key: %s\n", strerror(errno));
        return;
    }
    
    // Set file permissions to read/write only for owner
    chmod(api_key_path, S_IRUSR | S_IWUSR);
    
    fprintf(file, "%s", gemini_api_key);
    fclose(file);
    printf("API key telah disimpan secara otomatis\n");
}

// Fungsi untuk memuat API key dari file
void load_api_key() {
    char api_key_path[MAX_CMD_LEN];
    get_api_key_path(api_key_path, sizeof(api_key_path));
    
    FILE *file = fopen(api_key_path, "r");
    if (file == NULL) {
        // File tidak ada, ini normal jika belum pernah menyimpan API key
        return;
    }
    
    if (fgets(gemini_api_key, MAX_API_KEY_LEN, file) != NULL) {
        // Hapus newline jika ada
        gemini_api_key[strcspn(gemini_api_key, "\n")] = 0;
        
        if (strlen(gemini_api_key) >= 10) {
            is_api_key_set = 1;
            printf("API key Gemini berhasil dimuat\n");
        }
    }
    
    fclose(file);
}

// Fungsi untuk menghapus API key (logout)
void logout_api_key() {
    // Clear API key dari memori
    memset(gemini_api_key, 0, sizeof(gemini_api_key));
    is_api_key_set = 0;
    
    // Hapus file API key
    char api_key_path[MAX_CMD_LEN];
    get_api_key_path(api_key_path, sizeof(api_key_path));
    
    if (remove(api_key_path) == 0) {
        printf("API key telah dihapus. Anda telah logout.\n");
    } else {
        if (errno == ENOENT) {
            // File tidak ada, berarti memang belum ada API key
            printf("Tidak ada API key yang tersimpan. Anda telah logout.\n");
        } else {
            fprintf(stderr, "Error menghapus API key: %s\n", strerror(errno));
        }
    }
}

// Callback function untuk cURL untuk menangani respons dari API
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) {
        printf("Tidak cukup memori (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// Fungsi untuk mengonfigurasi API key
void setup_ai_api() {
    printf("Masukkan Google Gemini API Key Anda: ");
    fgets(gemini_api_key, MAX_API_KEY_LEN, stdin);
    
    // Menghapus newline dari input
    gemini_api_key[strcspn(gemini_api_key, "\n")] = 0;
    
    // Memeriksa apakah API key valid (setidaknya ada 10 karakter)
    if (strlen(gemini_api_key) < 10) {
        printf("API key terlalu pendek! API key tidak diatur.\n");
        is_api_key_set = 0;
    } else {
        printf("API key berhasil diatur.\n");
        is_api_key_set = 1;
        // Simpan API key
        save_api_key();
    }
}

// Fungsi untuk mengurai respons JSON untuk mendapatkan teks hasil
char* extract_text_from_json(const char* json) {
    static char result[MAX_RESPONSE_SIZE] = {0};
    
    // Reset result
    memset(result, 0, sizeof(result));
    
    // Debug: Print the received JSON response
    // printf("DEBUG - Received JSON: %s\n", json);
    
    // Try different markers that might exist in the response
    const char* markers[] = {
        "\"text\": \"",
        "\"text\":\"",
        "\"content\": \"",
        "\"content\":\"",
        "\"contents\":[{\"parts\":[{\"text\":\"",
        "\"parts\":[{\"text\":\"",
        "\"text\":\""
    };
    
    const char* text_start = NULL;
    
    // Try each marker
    for (int m = 0; m < sizeof(markers)/sizeof(markers[0]); m++) {
        text_start = strstr(json, markers[m]);
        if (text_start) {
            text_start += strlen(markers[m]);
            break;
        }
    }
    
    if (!text_start) {
        // Check if there's an error message
        const char* error_marker = "\"error\":";
        const char* error_start = strstr(json, error_marker);
        if (error_start) {
            return "Error dalam permintaan API. Periksa API key dan coba lagi.";
        }
        
        return "Tidak dapat menemukan teks dalam respons JSON. Format respons mungkin telah berubah.";
    }
    
    int i = 0;
    int in_escape = 0;
    
    // Salin teks sampai menemukan tanda kutip penutup yang bukan bagian dari escape sequence
    while (text_start[i] != '\0' && i < MAX_RESPONSE_SIZE - 1) {
        // Jika menemukan tanda kutip dan bukan escaped
        if (!in_escape && text_start[i] == '\"') {
            break;
        }
        
        // Jika karakter saat ini adalah backslash, tandai sebagai escape sequence
        if (text_start[i] == '\\' && !in_escape) {
            in_escape = 1;
        } else {
            // Interpretasikan escape sequence yang umum
            if (in_escape) {
                switch (text_start[i]) {
                    case 'n': result[strlen(result)] = '\n'; break;
                    case 't': result[strlen(result)] = '\t'; break;
                    case 'r': result[strlen(result)] = '\r'; break;
                    case '"': result[strlen(result)] = '"'; break;
                    case '\\': result[strlen(result)] = '\\'; break;
                    default: 
                        // Jika bukan escape sequence yang dikenal, salin escape character dan karakter saat ini
                        result[strlen(result)] = text_start[i];
                }
                in_escape = 0;
            } else {
                // Karakter normal, salin apa adanya
                result[strlen(result)] = text_start[i];
            }
        }
        
        i++;
    }
    
    // Pastikan string diakhiri dengan null terminator
    result[strlen(result)] = '\0';
    
    return result;
}

// Fungsi untuk membersihkan respon dari Gemini sebelum menjalankannya
char* clean_command(const char* response) {
    static char cleaned[MAX_CMD_LEN];
    int i = 0, j = 0;
    int in_backticks = 0;
    int in_markdown_block = 0;
    
    // Inisialisasi string kosong
    cleaned[0] = '\0';
    
    // Jika response kosong atau NULL, kembalikan string kosong
    if (response == NULL || response[0] == '\0') {
        return cleaned;
    }
    
    // Salin seluruh respons ke buffer sementara untuk diproses
    char temp[MAX_RESPONSE_SIZE];
    strncpy(temp, response, MAX_RESPONSE_SIZE - 1);
    temp[MAX_RESPONSE_SIZE - 1] = '\0';
    
    // Cari baris yang berisi perintah yang valid (misalnya dimulai dengan rm, cat, dll)
    char* valid_line = NULL;
    
    // Perintah yang valid untuk file operations
    const char* commands[] = {"rm ", "mv ", "cp ", "cat ", "touch ", "mkdir ", "chmod "};
    const int num_commands = 7;
    
    // Memecah respons menjadi baris
    char* line = strtok(temp, "\n");
    while (line != NULL) {
        // Lewati markdown delimiters (```)
        if (strncmp(line, "```", 3) == 0) {
            line = strtok(NULL, "\n");
            continue;
        }
        
        // Cek jika baris berisi perintah yang valid
        for (int cmd = 0; cmd < num_commands; cmd++) {
            if (strstr(line, commands[cmd]) != NULL) {
                valid_line = line;
                break;
            }
        }
        
        // Jika ditemukan perintah valid, hentikan pencarian
        if (valid_line != NULL) break;
        
        line = strtok(NULL, "\n");
    }
    
    // Jika baris valid tidak ditemukan, gunakan baris pertama
    if (valid_line == NULL) {
        // Reset temp dan coba ulang untuk mendapatkan baris pertama
        strncpy(temp, response, MAX_RESPONSE_SIZE - 1);
        temp[MAX_RESPONSE_SIZE - 1] = '\0';
        valid_line = strtok(temp, "\n");
    }
    
    // Jika masih NULL, kembalikan string kosong
    if (valid_line == NULL) {
        return cleaned;
    }
    
    // Bersihkan markdown dan whitespace
    i = 0;
    
    // Skip markdown opening jika ada
    if (strncmp(valid_line, "```", 3) == 0) {
        i = 3;
        while (valid_line[i] && !isspace(valid_line[i])) i++; // Skip language tag
        while (valid_line[i] && isspace(valid_line[i])) i++;  // Skip spaces
    }
    
    // Salin ke cleaned tanpa markdown/backticks
    while (valid_line[i] && j < MAX_CMD_LEN - 1) {
        if (valid_line[i] == '`') {
            i++;
            continue;
        }
        cleaned[j++] = valid_line[i++];
    }
    cleaned[j] = '\0';
    
    // Hapus whitespace di awal dan akhir
    char *start = cleaned;
    while (*start && isspace(*start)) start++;
    
    if (*start) {
        char *end = cleaned + strlen(cleaned) - 1;
        while (end > start && isspace(*end)) end--;
        *(end + 1) = '\0';
    }
    
    return start;
}

// Fungsi untuk mengirim perintah ke Gemini API dan mendapatkan respons
void ask_ai_terminal(const char* prompt) {
    if (!is_api_key_set) {
        printf("API key belum diatur. Silakan gunakan perintah 'ai setup' terlebih dahulu.\n");
        return;
    }
    
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    // Get current working directory for context
    char cwd[MAX_CMD_LEN];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strcpy(cwd, "unknown");
    }
    
    // Format prompt with more context including current directory
    char formatted_prompt[MAX_CMD_LEN * 6]; // Ukuran jauh lebih besar untuk menampung semua data
    
    // Jika permintaan terkait dengan file (hapus/delete/rm/touch/cat), jalankan ls terlebih dahulu
    if (strstr(prompt, "hapus") || strstr(prompt, "delete") || strstr(prompt, "rm") || 
        strstr(prompt, "touch") || strstr(prompt, "cat") || strstr(prompt, "edit") ||
        strstr(prompt, "file")) {
        
        // Dapatkan daftar file di direktori saat ini
        printf("Memeriksa daftar file di direktori saat ini...\n");
        
        FILE *fp;
        char file_list[MAX_CMD_LEN * 2] = ""; // Ukuran yang lebih besar
        char line[256];
        int line_count = 0;
        
        // Gunakan perintah ls yang lebih terfokus (tanpa hidden files)
        fp = popen("ls -l | head -20", "r");
        if (fp != NULL) {
            while (fgets(line, sizeof(line), fp) != NULL && line_count < 20) {
                strcat(file_list, line);
                line_count++;
            }
            pclose(fp);
        }
        
        // Sertakan informasi ini dalam prompt
        snprintf(formatted_prompt, sizeof(formatted_prompt), 
                 "Kamu adalah asisten terminal Linux yang membantu dengan perintah bash. "
                 "Tugas kamu adalah memberikan perintah bash yang tepat untuk: \"%s\". "
                 "PENTING: Direktori kerja saat ini adalah: %s. "
                 "Daftar file di direktori saat ini:\n%s\n"
                 "Ikuti aturan ini dengan cermat: "
                 "1. PERHATIKAN NAMA FILE DENGAN TEPAT sebelum membuat perintah. Pastikan nama file persis sesuai dengan yang ada di direktori. "
                 "2. Untuk file di direktori saat ini, SELALU gunakan awalan ./ (contoh: ./nama-file) "
                 "3. Untuk path dengan spasi, SELALU gunakan tanda kutip TUNGGAL, bukan ganda (contoh: './nama file') "
                 "4. Periksa apakah nama file misalnya 'test.txt' bukan 'tes.txt', perhatikan ejaan yang tepat "
                 "5. Berikan HANYA perintah bash yang valid, tanpa komentar, backtick, markdown, atau penjelasan "
                 "Jawaban kamu HANYA berisi perintah siap eksekusi, tidak ada teks tambahan.", 
                 prompt, cwd, file_list);
    } else {
        // Format prompt biasa tanpa daftar file
        snprintf(formatted_prompt, sizeof(formatted_prompt), 
                 "Kamu adalah asisten terminal Linux yang membantu dengan perintah bash. "
                 "Tugas kamu adalah memberikan perintah bash yang tepat untuk: \"%s\". "
                 "PENTING: Direktori kerja saat ini adalah: %s. "
                 "Ikuti aturan ini dengan cermat: "
                 "1. PERHATIKAN NAMA FILE DENGAN TEPAT sebelum membuat perintah. Pastikan nama file persis sesuai dengan yang ada di direktori. "
                 "2. Untuk file di direktori saat ini, SELALU gunakan awalan ./ (contoh: ./nama-file) "
                 "3. Untuk path dengan spasi, SELALU gunakan tanda kutip TUNGGAL, bukan ganda (contoh: './nama file') "
                 "4. Periksa apakah nama file misalnya 'test.txt' bukan 'tes.txt', perhatikan ejaan yang tepat "
                 "5. Berikan HANYA perintah bash yang valid, tanpa komentar, backtick, markdown, atau penjelasan "
                 "Jawaban kamu HANYA berisi perintah siap eksekusi, tidak ada teks tambahan.", 
                 prompt, cwd);
    }
    
    // Escape karakter khusus dalam prompt untuk JSON
    for (int i = 0; i < strlen(formatted_prompt); i++) {
        if (formatted_prompt[i] == '"') {
            memmove(&formatted_prompt[i+1], &formatted_prompt[i], strlen(&formatted_prompt[i]) + 1);
            formatted_prompt[i] = '\\';
            i++;
        }
        else if (formatted_prompt[i] == '\\') {
            memmove(&formatted_prompt[i+1], &formatted_prompt[i], strlen(&formatted_prompt[i]) + 1);
            i++;
        }
    }
    
    // Membangun JSON request - gunakan format yang benar untuk model Gemini
    char post_data[MAX_CMD_LEN * 8]; // Ukuran yang jauh lebih besar untuk menghindari truncation
    snprintf(post_data, sizeof(post_data), 
             "{"
             "\"contents\": [{"
             "\"parts\": [{\"text\": \"%s\"}]"
             "}]"
             "}", formatted_prompt);
    
    // Membuat URL dengan API key
    char url[256];
    snprintf(url, sizeof(url), 
             "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=%s", 
             gemini_api_key);
    
    // Inisialisasi cURL
    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        
        // Untuk debugging
        // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        
        printf("Mengirim permintaan ke Gemini AI...\n");
        
        // Melakukan request
        res = curl_easy_perform(curl);
        
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() gagal: %s\n", curl_easy_strerror(res));
        } else {
            // Debug: Tampilkan response mentah
            // printf("Response raw: %s\n", chunk.memory);
            
            // Mengurai respons dan menampilkan hasilnya
            char* response_text = extract_text_from_json(chunk.memory);
            
            printf("\n==== Respons dari Gemini AI ====\n");
            printf("%s\n", response_text);
            printf("================================\n\n");
            
            // Bersihkan perintah dari markdown dan backticks
            char* cleaned_command = clean_command(response_text);
            
            // Ganti karakter escape HTML dengan karakter aslinya
            replace_html_escapes(cleaned_command);
            
            // Periksa apakah perintah kosong setelah dibersihkan atau mengandung string error
            if (cleaned_command[0] == '\0' || 
                strstr(cleaned_command, "Tidak dapat") != NULL || 
                strstr(cleaned_command, "tidak dapat") != NULL ||
                strstr(cleaned_command, "Error") != NULL) {
                printf("Maaf, tidak dapat mengekstrak perintah yang valid dari respons AI.\n");
            } else {
                // Tanyakan apakah ingin menjalankan perintah tersebut
                printf("Perintah yang akan dijalankan: %s\n", cleaned_command);
                printf("Apakah Anda ingin menjalankan perintah ini? (y/n): ");
                char answer[10];
                fgets(answer, sizeof(answer), stdin);
                
                if (answer[0] == 'y' || answer[0] == 'Y') {
                    printf("Menjalankan perintah...\n");
                    
                    // Periksa apakah ini perintah cd
                    if (strncmp(cleaned_command, "cd ", 3) == 0) {
                        // Ekstrak direktori tujuan
                        char* target_dir = cleaned_command + 3;
                        
                        // Hilangkan spasi di awal jika ada
                        while (*target_dir == ' ') target_dir++;
                        
                        // Hapus tanda kutip yang mungkin mengelilingi path
                        char* clean_dir = remove_surrounding_quotes(target_dir);
                        
                        // Jalankan chdir langsung di proses utama
                        if (chdir(clean_dir) != 0) {
                            perror("cd");
                        }
                    } else if (strncmp(cleaned_command, "rm ", 3) == 0 || 
                               strncmp(cleaned_command, "touch ", 6) == 0 ||
                               strncmp(cleaned_command, "mkdir ", 6) == 0 ||
                               strncmp(cleaned_command, "rmdir ", 6) == 0 ||
                               strncmp(cleaned_command, "cp ", 3) == 0 ||
                               strncmp(cleaned_command, "mv ", 3) == 0 ||
                               strncmp(cleaned_command, "cat ", 4) == 0 ||
                               strncmp(cleaned_command, "echo ", 5) == 0) {
                        // Untuk perintah yang bekerja dengan file, tangani path dengan spasi
                        char bash_command[MAX_CMD_LEN * 2];
                        
                        // Membersihkan command dari tanda kutip ganda jika ada
                        char cleaned_path[MAX_CMD_LEN];
                        strncpy(cleaned_path, cleaned_command, MAX_CMD_LEN - 1);
                        cleaned_path[MAX_CMD_LEN - 1] = '\0';
                        
                        // Ganti semua tanda kutip ganda menjadi kutip tunggal jika ada
                        char* ptr = cleaned_path;
                        while ((ptr = strchr(ptr, '"')) != NULL) {
                            *ptr = '\'';
                            ptr++; // Pindahkan pointer ke karakter berikutnya untuk menghindari infinite loop
                        }
                        
                        // Pastikan perintah dengan spasi dalam filename ditangani dengan benar
                        snprintf(bash_command, sizeof(bash_command), "bash -c \"%s\"", cleaned_path);
                        
                        printf("Menjalankan: %s\n", cleaned_path);
                        // printf("Perintah bash: %s\n", bash_command); // Uncomment untuk debugging
                        
                        // Jalankan perintah
                        int result = system(bash_command);
                        
                        if (result != 0) {
                            printf("Perintah selesai dengan kode keluar: %d\n", WEXITSTATUS(result));
                        }
                    } else {
                        // Untuk perintah lain, gunakan system() seperti biasa
                        char bash_command[MAX_CMD_LEN + 20];
                        snprintf(bash_command, sizeof(bash_command), "bash -c \"%s\"", cleaned_command);
                        int result = system(bash_command);
                        
                        if (result != 0) {
                            printf("Perintah selesai dengan kode keluar: %d\n", WEXITSTATUS(result));
                        }
                    }
                } else {
                    printf("Perintah tidak dijalankan.\n");
                }
            }
        }
        
        // Membersihkan
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    
    free(chunk.memory);
}

// Deklarasi fungsi wifi_add
void wifi_add() {
    printf("Fungsi wifi_add belum diimplementasikan\n");
}

void setup_dns() {
    char ip[50], domain[100];
    char command[512];  // Ukuran buffer yang lebih besar untuk menghindari truncation
    char ip_parts[4][4]; // Array untuk memisahkan IP ke dalam bagian-bagian (oktets)

    // Meminta input dari user
    printf("Masukkan IP untuk DNS server (contoh 192.168.100.86): ");

    fgets(ip, sizeof(ip), stdin);
    ip[strcspn(ip, "\n")] = 0;  // Menghapus newline di akhir input

    // Memecah IP menjadi bagian-bagian (oktets)
    sscanf(ip, "%3s.%3s.%3s.%3s", ip_parts[0], ip_parts[1], ip_parts[2], ip_parts[3]);

    printf("Masukkan domain untuk DNS (contoh: sugax1-server.com): ");
    fgets(domain, sizeof(domain), stdin);
    domain[strcspn(domain, "\n")] = 0;  // Menghapus newline di akhir input

    // Mengecek apakah bind9 sudah terinstal
    printf("Memeriksa apakah bind9 sudah terinstal...\n");
    FILE *fp = popen("which bind9", "r");
    if (fp == NULL || feof(fp)) {
        // Jika bind9 belum terinstal, instal bind9
        printf("bind9 belum terinstal. Menginstal bind9...\n");
        system("sudo apt-get update");
        system("sudo apt-get install bind9 -y");
    } else {
        printf("bind9 sudah terinstal.\n");
    }

    // Menyalin file db.127 menjadi db.<ip_user>
    printf("Menyalin db.127 menjadi db.%s...\n", ip);
    snprintf(command, sizeof(command), "sudo cp /etc/bind/db.127 /etc/bind/db.%s", ip);
    system(command);

    // Mengedit file db.<ip_user>
    printf("Mengedit file db.%s...\n", ip);
    snprintf(command, sizeof(command), "sudo sed -i 's/localhost/%s/g' /etc/bind/db.%s", domain, ip);
    system(command);
    snprintf(command, sizeof(command), "sudo sed -i 's/127.0.0.1/%s/g' /etc/bind/db.%s", ip, ip);
    system(command);

    // Menyalin file db.local menjadi db.<domain_user>
    printf("Menyalin db.local menjadi db.%s...\n", domain);
    snprintf(command, sizeof(command), "sudo cp /etc/bind/db.local /etc/bind/db.%s", domain);
    system(command);

    // Mengedit file db.<domain_user>
    printf("Mengedit file db.%s...\n", domain);
    snprintf(command, sizeof(command), "sudo sed -i 's/localhost/%s/g' /etc/bind/db.%s", domain, domain);
    system(command);
    snprintf(command, sizeof(command), "sudo sed -i 's/127.0.0.1/%s/g' /etc/bind/db.%s", ip, domain);
    system(command);

    // Menambahkan konfigurasi di named.conf.local untuk zona domain
    printf("Menambahkan konfigurasi zona untuk domain di named.conf.local...\n");
    snprintf(command, sizeof(command), 
             "echo 'zone \"%s\" { type master; file \"/etc/bind/db.%s\"; allow-update { none; }; };' | sudo tee -a /etc/bind/named.conf.local", 
             domain, domain);
    system(command);

    // Menambahkan konfigurasi untuk zona reverse di named.conf.local
    // Membalikkan IP dan menggunakan tiga oktet pertama (misalnya, 192.168.100.86 menjadi 100.168.192)
    char reversed_ip[50];
    snprintf(reversed_ip, sizeof(reversed_ip), "%s.%s.%s", ip_parts[2], ip_parts[1], ip_parts[0]);
    
    printf("Menambahkan konfigurasi zona reverse di named.conf.local...\n");
    snprintf(command, sizeof(command), 
             "echo 'zone \"%s.in-addr.arpa\" { type master; file \"/etc/bind/db.%s\"; };' | sudo tee -a /etc/bind/named.conf.local", 
             reversed_ip, ip);
    system(command);

    // Mengedit /etc/resolv.conf untuk menambahkan IP DNS server
    printf("Menambahkan IP DNS ke /etc/resolv.conf...\n");
    snprintf(command, sizeof(command), "echo 'nameserver %s' | sudo tee -a /etc/resolv.conf", ip);
    system(command);

    // Mengganti 1.0.0 dengan oktet terakhir IP di file db.<ip_user>
    printf("Mengganti 1.0.0 dengan oktet terakhir IP di db.%s...\n", ip);
    snprintf(command, sizeof(command), "sudo sed -i 's/1.0.0/%s/g' /etc/bind/db.%s", ip_parts[3], ip);
    system(command);

    // Restart bind9 untuk menerapkan konfigurasi
    printf("Restarting bind9...\n");
    system("sudo systemctl restart bind9");

    // Menampilkan pesan sukses
    printf("\nKonfigurasi DNS selesai! DNS telah disiapkan dengan IP %s dan domain %s.\n", ip, domain);
}

// Fungsi untuk menampilkan prompt dengan direktori saat ini
char* show_prompt() {
    static char prompt[MAX_CMD_LEN];
    char cwd[MAX_CMD_LEN];
    
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        // Menghindari pemeriksaan truncation dengan menjamin ukuran yang cukup
        char prefix[] = "mishell-EDU [";
        char suffix[] = "]> ";
        size_t prefix_len = strlen(prefix);
        size_t suffix_len = strlen(suffix);
        size_t cwd_len = strlen(cwd);
        
        // Pastikan ukuran total tidak melebihi MAX_CMD_LEN - 1 (untuk null terminator)
        size_t max_total = MAX_CMD_LEN - 1;
        
        if (prefix_len + cwd_len + suffix_len > max_total) {
            // Jika terlalu panjang, potong path di tengah
            size_t max_cwd_len = max_total - prefix_len - suffix_len;
            size_t ellipsis_len = 3; // untuk "..."
            
            if (max_cwd_len > ellipsis_len + 10) { // Pastikan masih ada ruang yang cukup setelah pemotongan
                // Tampilkan bagian awal dan akhir dari path dengan "..." di tengah
                size_t first_part = (max_cwd_len - ellipsis_len) / 2;
                size_t second_part = max_cwd_len - ellipsis_len - first_part;
                
                // Copy prefix
                strncpy(prompt, prefix, prefix_len);
                
                // Copy bagian awal path
                strncpy(prompt + prefix_len, cwd, first_part);
                
                // Tambahkan "..."
                strcpy(prompt + prefix_len + first_part, "...");
                
                // Copy bagian akhir path
                strncpy(prompt + prefix_len + first_part + ellipsis_len, 
                       cwd + (cwd_len - second_part), second_part);
                
                // Tambahkan suffix
                strcpy(prompt + prefix_len + first_part + ellipsis_len + second_part, suffix);
            } else {
                // Jika terlalu pendek, gunakan fallback
                strcpy(prompt, "mishell-EDU> ");
            }
        } else {
            // Jika cukup ruang, gabungkan secara normal
            strcpy(prompt, prefix);
            strcat(prompt, cwd);
            strcat(prompt, suffix);
        }
    } else {
        perror("getcwd");
        strcpy(prompt, "mishell-EDU> ");
    }
    
    return prompt;
}

// Fungsi untuk menampilkan daftar perintah yang tersedia
void list_commands() {
    printf("\nDaftar perintah yang tersedia:\n");
    printf("1. cd <direktori>      : Pindah direktori\n");
    printf("2. echo <pesan>        : Tampilkan pesan\n");
    printf("3. touch <file>        : Membuat file baru\n");
    printf("4. edit <file>         : Mengedit file\n");
    printf("5. q                   : Keluar dari shell\n");
    printf("6. history             : Menampilkan riwayat perintah\n");
    printf("7. pwd                 : Menampilkan direktori saat ini\n");
    printf("8. ls                  : Menampilkan daftar file\n");
    printf("9. cat <file>          : Menampilkan isi file\n");
    printf("10. rm <file>          : Menghapus file\n");
    printf("11. rmdir <dir>        : Menghapus direktori kosong\n");
    printf("12. mkdir <dir>        : Membuat direktori baru\n");
    printf("13. clear / cl         : Menghapus layar terminal\n");
    printf("14. cp <source> <dest> : Menyalin file\n");
    printf("15. mv <source> <dest> : Memindahkan file\n");
    printf("16. whoami             : Menampilkan nama pengguna\n");
    printf("17. date               : Menampilkan tanggal dan waktu\n");
    printf("18. man <command>      : Menampilkan manual perintah\n");
    printf("19. head <file>        : Menampilkan beberapa baris pertama file\n");
    printf("20. tail <file>        : Menampilkan beberapa baris terakhir file\n");
    printf("21. setup dns          : Meng-Setup DNS dengan cepat\n");
    printf("22. list perintah      : Menampilkan daftar perintah yang tersedia\n");
    printf("23. cek battery        : Memeriksa kapasitas baterai\n");
    printf("24. test speed         : Menguji kecepatan internet\n");
    printf("25. cek cpu            : Menampilkan informasi penggunaan CPU\n");
    printf("26. cek ram            : Menampilkan informasi penggunaan RAM\n");
    printf("27. cek disk           : Menampilkan informasi penggunaan disk\n");
    printf("28. ai setup           : Menyiapkan Google Gemini API\n");
    printf("29. ai <pertanyaan>    : Bertanya ke AI tentang perintah terminal\n");
    printf("30. ai logout          : Menghapus API key Gemini yang tersimpan\n");
    printf("\nSilakan masukkan perintah!\n");
}


// Fungsi untuk menambahkan perintah ke history
void add_to_history(char* input) {
    if (input == NULL || input[0] == '\0')
        return;
        
    if (history_count < MAX_HISTORY) {
        strcpy(history[history_count], input);
        history_count++;
    } else {
        for (int i = 1; i < MAX_HISTORY; i++) {
            strcpy(history[i-1], history[i]);
        }
        strcpy(history[MAX_HISTORY-1], input);
    }
    
    // Juga tambahkan ke readline history
    add_history(input);
}

// Fungsi untuk menampilkan history
void show_history() {
    for (int i = 0; i < history_count; i++) {
        printf("%d: %s", i + 1, history[i]);
    }
}

// Fungsi untuk mem-parsing input perintah
void parse_input(char* input, char** args) {
    char* token = strtok(input, " \n");
    int i = 0;

    while (token != NULL) {
        args[i++] = token;
        token = strtok(NULL, " \n");
    }
    args[i] = NULL;
}

// Fungsi untuk menangani input/output redirection
void handle_redirection(char** args) {
    int i = 0;
    int input_redirect = -1, output_redirect = -1;
    char* input_file = NULL, *output_file = NULL;

    // Menentukan posisi input/output redirection
    while (args[i] != NULL) {
        if (strcmp(args[i], "<") == 0) {
            input_redirect = i;
            input_file = args[i + 1];
        } else if (strcmp(args[i], ">") == 0) {
            output_redirect = i;
            output_file = args[i + 1];
        }
        i++;
    }

    if (input_redirect != -1) {
        // Input redirection
        int fd = open(input_file, O_RDONLY);
        if (fd == -1) {
            perror("Error opening input file");
            return;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
        args[input_redirect] = NULL;  // Hapus "<" dan file name
    }

    if (output_redirect != -1) {
        // Output redirection
        int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("Error opening output file");
            return;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
        args[output_redirect] = NULL;  // Hapus ">" dan file name
    }
}

// Fungsi untuk mengeksekusi perintah dengan atau tanpa pipe
void execute_pipeline(char* input) {
    char* commands[MAX_ARGS];
    int num_commands = 0;

    // Memecah perintah berdasarkan pipe "|"
    char* cmd = strtok(input, "|");
    while (cmd != NULL) {
        commands[num_commands++] = cmd;
        cmd = strtok(NULL, "|");
    }

    int pipefd[2 * (num_commands - 1)][2];
    pid_t pid;

    for (int i = 0; i < num_commands; i++) {
        pipe(pipefd[i]);

        pid = fork();
        if (pid == 0) {
            // Child process
            if (i > 0) {
                dup2(pipefd[i - 1][0], STDIN_FILENO);  // Dapatkan input dari pipe sebelumnya
            }
            if (i < num_commands - 1) {
                dup2(pipefd[i][1], STDOUT_FILENO);  // Kirim output ke pipe berikutnya
            }

            char* args[MAX_ARGS];
            parse_input(commands[i], args);
            handle_redirection(args);
            execvp(args[0], args);
            perror("execvp");  // Menambahkan error handling di sini
            exit(1);
        } else if (pid < 0) {
            perror("fork");
        }
    }

    // Menutup semua pipe dan menunggu semua child process
    for (int i = 0; i < num_commands - 1; i++) {
        close(pipefd[i][0]);
        close(pipefd[i][1]);
    }
    for (int i = 0; i < num_commands; i++) {
        wait(NULL);
    }
}

// Fungsi untuk mengeksekusi perintah dengan atau tanpa pipe
void execute_command(char** args) {
    if (args[0] == NULL) return;  // Tidak ada perintah untuk dijalankan

    // Perintah internal "cd"
    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "cd: expected argument\n");
        } else if (chdir(args[1]) != 0) {
            perror("cd");
        }
    }
    // Perintah internal "exit"
    else if (strcmp(args[0], "q") == 0) {
        exit(0);
    }
    // Perintah internal "setup dns"
    else if (strcmp(args[0], "setup") == 0 && strcmp(args[1], "dns") == 0) {
        setup_dns();  // Memanggil fungsi setup_dns
    }
    // Perintah AI setup
    else if (strcmp(args[0], "ai") == 0 && args[1] != NULL && strcmp(args[1], "setup") == 0) {
        setup_ai_api();
    }
    // Perintah AI logout
    else if (strcmp(args[0], "ai") == 0 && args[1] != NULL && 
             (strcmp(args[1], "logout") == 0 || strcmp(args[1], "keluar") == 0)) {
        logout_api_key();
    }
    // Perintah AI ask
    else if (strcmp(args[0], "ai") == 0 && args[1] != NULL) {
        // Menggabungkan semua argumen setelah "ai" menjadi satu prompt
        char prompt[MAX_CMD_LEN] = "";
        for (int i = 1; args[i] != NULL; i++) {
            strcat(prompt, args[i]);
            strcat(prompt, " ");
        }
        
        ask_ai_terminal(prompt);
    }
    // Perintah internal "wifi add"
    else if (strcmp(args[0], "wifi") == 0 && strcmp(args[1], "add") == 0) {
        wifi_add();  // Memanggil fungsi wifi_add
    }
    // Perintah internal "list perintah"
    else if (strcmp(args[0], "list") == 0 && strcmp(args[1], "perintah") == 0) {
        list_commands();
    }
    // Perintah internal "cek battery"
    else if (strcmp(args[0], "cek") == 0 && strcmp(args[1], "battery") == 0) {
        FILE *fp = fopen("/sys/class/power_supply/BAT0/capacity", "r");
        if (fp == NULL) {
            perror("Gagal membuka file kapasitas baterai");
            return;
        }

        int capacity;
        if (fscanf(fp, "%d", &capacity) != 1) {
            perror("Gagal membaca kapasitas baterai");
            fclose(fp);
            return;
        }

        printf("Kapasitas Baterai: %d%%\n", capacity);
        fclose(fp);
    }
    // Test Speed
    else if (strcmp(args[0], "test") == 0 && strcmp(args[1], "speed") == 0) {
        // Memeriksa apakah speedtest-cli sudah terinstal
        printf("Memeriksa apakah speedtest-cli sudah terinstal...\n");

        // Cek dengan lebih akurat
        int status = system("which speedtest-cli > /dev/null 2>&1");
        
        if (status != 0) {
            printf("speedtest-cli tidak ditemukan. Menginstal...\n");
            
            // Menginstal speedtest-cli jika belum terinstal
            system("sudo apt-get update && sudo apt-get install -y speedtest-cli");

            // Periksa lagi setelah instalasi
            status = system("which speedtest-cli > /dev/null 2>&1");
            if (status != 0) {
                printf("Gagal menginstal speedtest-cli. Silakan instal manual dengan:\n");
                printf("sudo apt-get install speedtest-cli\n");
                return;
            }
            
        printf("speedtest-cli telah terinstal.\n");
    } else {
        printf("speedtest-cli sudah terinstal.\n");
    }
    
    // Menampilkan pesan animasi sementara menunggu hasil tes
    printf("Mengukur kecepatan");
    
    // Animasi titik bertahap
    for (int i = 0; i < 3; i++) {
        printf(".");
        fflush(stdout);  // Mengeluarkan output ke terminal tanpa buffer
        sleep(1);        // Menunggu 1 detik
    }
    printf("\n");

        // Menggunakan path absolut sebagai tambahan keamanan
    printf("\033[1;32m");  // Mengaktifkan warna hijau terang
        
        // Menggunakan execvp untuk menjalankan speedtest-cli dengan lebih aman
        pid_t pid = fork();
        if (pid == 0) {
            // Proses anak
            execlp("speedtest-cli", "speedtest-cli", "--simple", NULL);
            
            // Jika execlp gagal, berikan pesan error
            perror("Gagal menjalankan speedtest-cli");
            exit(1);
        } else if (pid < 0) {
            // Fork gagal
            perror("Fork gagal");
        } else {
            // Proses parent
            int status;
            waitpid(pid, &status, 0);
            
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                printf("Gagal menjalankan speedtest-cli.\n");
                printf("Coba instal ulang dengan: sudo apt-get install --reinstall speedtest-cli\n");
            }
        }
        
    printf("\033[0m");  // Reset warna ke default

    // Memberikan jeda singkat untuk efek
    printf("\nTes kecepatan selesai!\n");
    }
    // Perintah internal "history"
    else if (strcmp(args[0], "history") == 0) {
        show_history();
    }
    // Perintah internal "pwd"
    else if (strcmp(args[0], "pwd") == 0) {
        char cwd[MAX_CMD_LEN];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("pwd");
        }
    }
    // Perintah internal "clear" atau "cl" untuk membersihkan layar
    else if (strcmp(args[0], "clear") == 0 || strcmp(args[0], "cl") == 0) {
        // Clear layar terminal
        printf("\033[H\033[J");  // ANSI escape sequence untuk membersihkan layar
    }
    // Perintah cek cpu, ram, dan disk
    else if (strcmp(args[0], "cek") == 0 && args[1] != NULL) {
        if (strcmp(args[1], "cpu") == 0) {
            check_cpu();
        } else if (strcmp(args[1], "ram") == 0) {
            check_ram();
        } else if (strcmp(args[1], "disk") == 0) {
            check_disk();
        } else if (strcmp(args[1], "battery") == 0) {
            FILE *fp = fopen("/sys/class/power_supply/BAT0/capacity", "r");
            if (fp == NULL) {
                perror("Gagal membuka file kapasitas baterai");
                return;
            }

            int capacity;
            if (fscanf(fp, "%d", &capacity) != 1) {
                perror("Gagal membaca kapasitas baterai");
                fclose(fp);
                return;
            }

            printf("Kapasitas Baterai: %d%%\n", capacity);
            fclose(fp);
        } else {
            printf("Perintah cek tidak dikenal: %s\n", args[1]);
            printf("Gunakan: cek cpu, cek ram, cek disk, atau cek battery\n");
        }
    }
    // Perintah internal "ls"
    else if (strcmp(args[0], "ls") == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            execvp(args[0], args);
            perror("execvp");  // Jika execvp gagal, tampilkan pesan error
            exit(1);  // Pastikan anak keluar setelah error
        } else if (pid < 0) {
            perror("fork");
        } else {
            waitpid(pid, NULL, 0);  // Tunggu proses anak selesai
        }
    }
    // Perintah internal "edit"
    else if (strcmp(args[0], "edit") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "edit: expected file name\n");
        } else {
            // Mengecek apakah file ada
            if (access(args[1], F_OK) != 0) {
                perror("edit");
                return;
            }

            // Memilih editor yang sesuai (misalnya nano atau vim)
            pid_t pid = fork();
            if (pid == 0) {
                // Menjalankan editor
                char *editor = "nano";  // Bisa ganti ke "vim" jika lebih suka vim
                execlp(editor, editor, args[1], (char *)NULL);
                perror("execvp");  // Jika execlp gagal, tampilkan pesan error
                exit(1);
            } else if (pid < 0) {
                perror("fork");
            } else {
                waitpid(pid, NULL, 0);  // Tunggu proses anak selesai
            }
        }
    }
    else {
        pid_t pid = fork();
        if (pid == 0) {
            execvp(args[0], args);
            perror("execvp");  // Jika execvp gagal, tampilkan pesan error
            exit(1);  // Pastikan anak keluar setelah error
        } else if (pid < 0) {
            perror("fork");
        } else {
            waitpid(pid, NULL, 0);  // Tunggu proses selesai
        }
    }
}

// Fungsi untuk menampilkan pesan selamat datang
void welcome_message() {
    printf("\n\n");
    printf("               /$$           /$$                 /$$      /$$$$$$$$  /$$$$$$$   /$$   /$$\n");
    printf("              |__/          | $$                | $$     |_____ $$/ | $$____  \\| $$  | $$\n");
    printf(" /$$$$$$/$$$$  /$$  /$$$$$$$| $$$$$$$   /$$$$$$ | $$          /$$/ | $$    \\ \\| $$  | $$\n");
    printf("| $$_  $$_  $$| $$ /$$_____/| $$__  $$ /$$__  $$| $$ /$$$$$$ /$$/ / | $$$$$$$$\\| $$  | $$\n");
    printf("| $$ \\ $$ \\ $$| $$|  $$$$$$ | $$  \\ $$| $$$$$$$$| $$|______//$$/ / | $$____ / | $$  | $$\n");
    printf("| $$ | $$ | $$| $$ \\____  $$| $$  | $$| $$_____/| $$       /$$/ /  | $$      | $$  | $$\n");
    printf("| $$ | $$ | $$| $$ /$$$$$$$/| $$  | $$|  $$$$$$$| $$      /$$/ /   | $$      |  $$$$$$/\n");
    printf("|__/ |__/ |__/|__/|_______/ |__/  |__/ \\_______/|__/     |__/ /    |__/       \\______/\n");
    printf("                                      /$$$$$$$$  /$$$$$$$  /$$   /$$          \n");
    printf("                                     | $$_____/ | $$__  $$| $$  | $$          \n");
    printf("                                     | $$       | $$  \\ $$| $$  | $$          \n");
    printf("                                     | $$$$$    | $$  | $$| $$  | $$          \n");
    printf("                                     | $$__/    | $$  | $$| $$  | $$          \n");
    printf("                                     | $$       | $$  | $$| $$  | $$          \n");
    printf("                                     | $$$$$$$$| $$$$$$$/ |  $$$$$$/          \n");
    printf("                                     |________/|_______/   \\______/           \n");
    printf("                                                                                      \n");
    printf("                                original by: azmi, zikri, dea, latief, rachel         \n");
    printf("                                edit by    : friza, gita, dimas, tirangga, syahran    \n");
    printf("*\n");
    printf("Perintah dasar yang dapat digunakan:\n");
    printf("1. cd <direktori>     : Pindah direktori\n");
    printf("2. echo <pesan>       : Tampilkan pesan\n");
    printf("3. touch <file>       : Membuat file baru\n");
    printf("4. edit <file>        : Mengedit file\n");
    printf("5. q                  : Keluar dari shell\n");
    printf("6. history            : Menampilkan riwayat perintah\n");
    printf("7. pwd                : Menampilkan direktori saat ini\n");
    printf("8. ls                 : Menampilkan daftar file\n");
    printf("9. cat <file>         : Menampilkan isi file\n");
    printf("10. rm <file>         : Menghapus file\n");
    printf("11. rmdir <dir>       : Menghapus direktori kosong\n");
    printf("12. mkdir <dir>       : Membuat direktori baru\n");
    printf("13. clear             : Menghapus layar terminal\n");
    printf("14. cp <source> <dest>: Menyalin file\n");
    printf("15. mv <source> <dest>: Memindahkan file\n");
    printf("16. whoami            : Menampilkan nama pengguna\n");
    printf("17. date              : Menampilkan tanggal dan waktu\n");
    printf("18. man <command>     : Menampilkan manual perintah\n");
    printf("19. head <file>       : Menampilkan beberapa baris pertama file\n");
    printf("20. tail <file>       : Menampilkan beberapa baris terakhir file\n");
    printf("21. setup dns         : Meng-Setup DNS dengan cepat\n");
    printf("22. list perintah     : Menampilkan daftar perintah yang tersedia\n");
    printf("23. cek battery       : Memeriksa kapasitas baterai\n");   
    printf("24. test speed        : Menguji kecepatan internet\n");
    printf("25. cek cpu           : Menampilkan informasi penggunaan CPU\n");
    printf("26. cek ram           : Menampilkan informasi penggunaan RAM\n");
    printf("27. cek disk          : Menampilkan informasi penggunaan disk\n");
    printf("28. ai setup          : Menyiapkan Google Gemini API\n");
    printf("29. ai <pertanyaan>   : Bertanya ke AI tentang perintah terminal\n");
    printf("30. ai logout         : Menghapus API key Gemini yang tersimpan\n");
    printf("\nSilakan masukkan perintah!\n");
}

// Fungsi untuk menghilangkan karakter kutip yang mengelilingi string
char* remove_surrounding_quotes(char* str) {
    static char result[MAX_CMD_LEN];
    
    // Copy string asli
    strncpy(result, str, MAX_CMD_LEN - 1);
    result[MAX_CMD_LEN - 1] = '\0';
    
    int len = strlen(result);
    
    // Hapus tanda kutip di awal dan akhir jika ada
    if (len >= 2 && ((result[0] == '"' && result[len-1] == '"') || 
                     (result[0] == '\'' && result[len-1] == '\''))) {
        memmove(result, result + 1, len - 2);
        result[len - 2] = '\0';
    }
    
    return result;
}

// Fungsi untuk meng-escape spasi dan karakter khusus dalam path
char* escape_path(const char* path) {
    static char escaped[MAX_CMD_LEN * 2];
    int i, j = 0;
    
    for (i = 0; path[i] != '\0' && j < MAX_CMD_LEN * 2 - 2; i++) {
        if (path[i] == ' ' || path[i] == '(' || path[i] == ')' || 
            path[i] == '[' || path[i] == ']' || path[i] == '&' || 
            path[i] == ';' || path[i] == '$' || path[i] == '|' || 
            path[i] == '<' || path[i] == '>' || path[i] == '`' || 
            path[i] == '\\' || path[i] == '"' || path[i] == '\'') {
            escaped[j++] = '\\';
        }
        escaped[j++] = path[i];
    }
    
    escaped[j] = '\0';
    return escaped;
}

// Fungsi untuk mengganti escape HTML dengan karakter sebenarnya
void replace_html_escapes(char* str) {
    if (str == NULL) return;
    
    // Daftar penggantian karakter escape HTML umum
    struct {
        const char* escape;
        const char* replacement;
    } replacements[] = {
        {"u003c", "<"},
        {"u003e", ">"},
        {"u0026", "&"},
        {"u0022", "\""},
        {"u0027", "'"},
        {"u002F", "/"},
        {"u005C", "\\"},
        {"&lt;", "<"},
        {"&gt;", ">"},
        {"&amp;", "&"},
        {"&quot;", "\""},
        {"&apos;", "'"},
        {NULL, NULL}  // Penanda akhir array
    };
    
    char* found;
    for (int i = 0; replacements[i].escape != NULL; i++) {
        size_t escape_len = strlen(replacements[i].escape);
        size_t repl_len = strlen(replacements[i].replacement);
        
        char* pos = str;
        while ((found = strstr(pos, replacements[i].escape)) != NULL) {
            // Simpan posisi setelah escape sequence
            char* after = found + escape_len;
            
            // Ganti escape dengan karakter asli
            memcpy(found, replacements[i].replacement, repl_len);
            
            // Geser sisa string
            if (escape_len != repl_len) {
                memmove(found + repl_len, after, strlen(after) + 1);
            }
            
            // Update posisi pencarian berikutnya
            pos = found + repl_len;
        }
    }
}

// Fungsi untuk memeriksa penggunaan CPU
void check_cpu() {
    printf("Memeriksa penggunaan CPU...\n");
    
    // Dapatkan jumlah core CPU terlebih dahulu
    int cpu_count = 0;
    char model_name[256] = "Unknown";
    
    FILE *fp_info = fopen("/proc/cpuinfo", "r");
    if (fp_info != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), fp_info)) {
            if (strncmp(line, "model name", 10) == 0) {
                // Ekstrak nama model
                char* colon = strchr(line, ':');
                if (colon != NULL) {
                    strncpy(model_name, colon + 2, sizeof(model_name) - 1);
                    model_name[sizeof(model_name) - 1] = '\0';
                    // Hapus newline di akhir
                    size_t len = strlen(model_name);
                    if (len > 0 && model_name[len - 1] == '\n') {
                        model_name[len - 1] = '\0';
                    }
                }
                cpu_count++;
            }
        }
        fclose(fp_info);
    }
    
    // Buka file /proc/stat yang berisi informasi CPU
    FILE *fp = fopen("/proc/stat", "r");
    if (fp == NULL) {
        perror("Gagal membuka file /proc/stat");
        return;
    }
    
    // Array untuk menyimpan data CPU (user, nice, system, idle)
    unsigned long long int cpu_times1[10] = {0};
    unsigned long long int cpu_times2[10] = {0};
    
    // Baca nilai awal
    char cpu_label[10];
    fscanf(fp, "%s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", 
           cpu_label, &cpu_times1[0], &cpu_times1[1], &cpu_times1[2], &cpu_times1[3], 
           &cpu_times1[4], &cpu_times1[5], &cpu_times1[6], &cpu_times1[7], 
           &cpu_times1[8], &cpu_times1[9]);
    
    fclose(fp);
    
    // Tunggu sebentar untuk melihat perubahan
    printf("Mengambil sampel penggunaan CPU untuk 1 detik...\n");
    
    // Menggunakan sleep lebih lama untuk sampel yang lebih akurat
    sleep(2);
    
    // Baca nilai kedua
    fp = fopen("/proc/stat", "r");
    if (fp == NULL) {
        perror("Gagal membuka file /proc/stat");
        return;
    }
    
    fscanf(fp, "%s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", 
           cpu_label, &cpu_times2[0], &cpu_times2[1], &cpu_times2[2], &cpu_times2[3], 
           &cpu_times2[4], &cpu_times2[5], &cpu_times2[6], &cpu_times2[7], 
           &cpu_times2[8], &cpu_times2[9]);
    
    fclose(fp);
    
    // Hitung total untuk kedua pengukuran
    unsigned long long int total_time1 = 0;
    unsigned long long int total_time2 = 0;
    
    for (int i = 0; i < 10; i++) {
        total_time1 += cpu_times1[i];
        total_time2 += cpu_times2[i];
    }
    
    // Hitung idle time untuk kedua pengukuran (iowait tidak dihitung sebagai idle)
    unsigned long long int idle_time1 = cpu_times1[3]; // idle saja
    unsigned long long int idle_time2 = cpu_times2[3]; // idle saja
    
    // Hitung delta
    unsigned long long int total_delta = total_time2 - total_time1;
    unsigned long long int idle_delta = idle_time2 - idle_time1;
    
    // Hitung persentase penggunaan CPU: non-idle / total
    double cpu_usage = 100.0 * (1.0 - ((double)idle_delta / total_delta));
    
    // Metode alternatif: Periksa penggunaan dengan top
    double top_usage = 0.0;
    FILE *fp_top = popen("top -bn1 | grep '%Cpu(s)' | awk '{print $2+$4+$6}'", "r");
    if (fp_top != NULL) {
        if (fscanf(fp_top, "%lf", &top_usage) != 1) {
            top_usage = cpu_usage; // Fallback ke nilai yang dihitung
        }
        pclose(fp_top);
    }
    
    // Gunakan nilai yang lebih masuk akal
    double final_cpu_usage = (top_usage > 0.0) ? top_usage : cpu_usage;
    
    // Tampilkan hasil dengan format yang bagus
    printf("\n");
    printf("\033[1;36m"); // Cyan terang
    printf("=================== PENGGUNAAN CPU ===================\n");
    printf("\033[0m"); // Reset warna
    
    printf("Penggunaan CPU Saat Ini: ");
    
    // Warna berdasarkan penggunaan
    if (final_cpu_usage < 50) {
        printf("\033[1;32m"); // Hijau terang
    } else if (final_cpu_usage < 80) {
        printf("\033[1;33m"); // Kuning terang
    } else {
        printf("\033[1;31m"); // Merah terang
    }
    
    printf("%.2f%%\n", final_cpu_usage);
    printf("\033[0m"); // Reset warna
    
    // Tampilkan informasi tambahan
    printf("\n");
    printf("CPU Model: %s\n", model_name);
    printf("Jumlah Core CPU: %d\n", cpu_count);
    
    printf("\n");
    printf("\033[1;36m"); // Cyan terang
    printf("=====================================================\n");
    printf("\033[0m"); // Reset warna
}

// Fungsi untuk memeriksa penggunaan RAM
void check_ram() {
    printf("Memeriksa penggunaan RAM...\n");
    
    // Buka file /proc/meminfo yang berisi informasi memori
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp == NULL) {
        perror("Gagal membuka file /proc/meminfo");
        return;
    }
    
    // Variabel untuk menyimpan informasi memori
    unsigned long total_mem = 0;
    unsigned long free_mem = 0;
    unsigned long available_mem = 0;
    unsigned long buffers = 0;
    unsigned long cached = 0;
    
    char line[256];
    
    // Baca file baris per baris
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %lu kB", &total_mem);
        } else if (strncmp(line, "MemFree:", 8) == 0) {
            sscanf(line, "MemFree: %lu kB", &free_mem);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "MemAvailable: %lu kB", &available_mem);
        } else if (strncmp(line, "Buffers:", 8) == 0) {
            sscanf(line, "Buffers: %lu kB", &buffers);
        } else if (strncmp(line, "Cached:", 7) == 0) {
            sscanf(line, "Cached: %lu kB", &cached);
        }
    }
    
    fclose(fp);
    
    // Hitung penggunaan memori
    unsigned long used_mem = total_mem - free_mem;
    double used_percent = 100.0 * ((double)used_mem / total_mem);
    
    // Konversi ke unit yang lebih mudah dibaca
    double total_gb = total_mem / 1024.0 / 1024.0;
    double used_gb = used_mem / 1024.0 / 1024.0;
    double free_gb = free_mem / 1024.0 / 1024.0;
    double available_gb = available_mem / 1024.0 / 1024.0;
    double cached_gb = cached / 1024.0 / 1024.0;
    double buffers_gb = buffers / 1024.0 / 1024.0;
    
    // Perbaikan: Gunakan "MemAvailable" untuk perhitungan yang lebih akurat
    unsigned long actual_used_mem = total_mem - available_mem;
    double actual_used_percent = 100.0 * ((double)actual_used_mem / total_mem);
    double actual_used_gb = actual_used_mem / 1024.0 / 1024.0;
    
    // Tampilkan hasil dengan format yang bagus
    printf("\n");
    printf("\033[1;36m"); // Cyan terang
    printf("=================== PENGGUNAAN RAM ===================\n");
    printf("\033[0m"); // Reset warna
    
    printf("Total RAM       : %.2f GB\n", total_gb);
    printf("Terpakai        : ");
    
    // Warna berdasarkan penggunaan
    if (actual_used_percent < 50) {
        printf("\033[1;32m"); // Hijau terang
    } else if (actual_used_percent < 80) {
        printf("\033[1;33m"); // Kuning terang
    } else {
        printf("\033[1;31m"); // Merah terang
    }
    
    printf("%.2f GB (%.2f%%)\n", actual_used_gb, actual_used_percent);
    printf("\033[0m"); // Reset warna
    
    printf("Tersedia        : %.2f GB\n", available_gb);
    printf("Bebas           : %.2f GB\n", free_gb);
    printf("Cache           : %.2f GB\n", cached_gb);
    printf("Buffer          : %.2f GB\n", buffers_gb);
    
    printf("\n");
    printf("\033[1;36m"); // Cyan terang
    printf("=====================================================\n");
    printf("\033[0m"); // Reset warna
}

// Fungsi untuk memeriksa penggunaan disk
void check_disk() {
    printf("Memeriksa penggunaan disk...\n");
    
    // Jalankan perintah df untuk mendapatkan informasi disk
    FILE *fp = popen("df -h --output=target,size,used,avail,pcent,fstype | grep -v tmpfs | grep -v loop", "r");
    if (fp == NULL) {
        perror("Gagal menjalankan perintah df");
        return;
    }
    
    char line[256];
    int header_skipped = 0;
    
    // Tampilkan hasil dengan format yang bagus
    printf("\n");
    printf("\033[1;36m"); // Cyan terang
    printf("=================== PENGGUNAAN DISK ===================\n");
    printf("\033[0m"); // Reset warna
    
    printf("%-15s %-10s %-10s %-10s %-10s %s\n", 
           "Mount Point", "Ukuran", "Terpakai", "Tersedia", "Persentase", "Tipe");
    printf("-----------------------------------------------------------------------\n");
    
    // Baca output dari perintah df
    while (fgets(line, sizeof(line), fp) != NULL) {
        // Lewati header
        if (!header_skipped) {
            header_skipped = 1;
            continue;
        }
        
        // Parse informasi disk
        char mount_point[64];
        char size[16];
        char used[16];
        char avail[16];
        char percent[16];
        char fstype[16];
        
        sscanf(line, "%s %s %s %s %s %s", mount_point, size, used, avail, percent, fstype);
        
        // Tampilkan dengan warna berdasarkan persentase penggunaan
        printf("%-15s %-10s ", mount_point, size);
        
        // Ekstrak nilai numerik dari persentase
        int used_percent = 0;
        sscanf(percent, "%d%%", &used_percent);
        
        // Warna berdasarkan penggunaan
        if (used_percent < 50) {
            printf("\033[1;32m"); // Hijau terang
        } else if (used_percent < 80) {
            printf("\033[1;33m"); // Kuning terang
        } else {
            printf("\033[1;31m"); // Merah terang
        }
        
        printf("%-10s ", used);
        printf("\033[0m"); // Reset warna
        
        printf("%-10s %-10s %s\n", avail, percent, fstype);
    }
    
    pclose(fp);
    
    // Tambahkan informasi I/O disk jika tersedia
    fp = popen("iostat -d -k | grep -v loop | grep -v ram | grep -v tmpfs | tail -n +4", "r");
    if (fp != NULL) {
        printf("\n");
        printf("Statistik I/O Disk (kB/s):\n");
        printf("%-15s %-10s %-10s\n", "Device", "Baca", "Tulis");
        printf("----------------------------------\n");
        
        while (fgets(line, sizeof(line), fp) != NULL) {
            char dev[16];
            float tps, kb_read, kb_wrtn;
            
            if (sscanf(line, "%s %f %f %f", dev, &tps, &kb_read, &kb_wrtn) == 4) {
                printf("%-15s %-10.2f %-10.2f\n", dev, kb_read, kb_wrtn);
            }
        }
        
        pclose(fp);
    }
    
    printf("\n");
    printf("\033[1;36m"); // Cyan terang
    printf("=======================================================\n");
    printf("\033[0m"); // Reset warna
}

int main() {
    char* input;
    char* args[MAX_ARGS];
    char input_copy[MAX_CMD_LEN];

    // Inisialisasi readline
    rl_bind_key('\t', rl_complete);
    
    // Load API key
    load_api_key();
    
    // Tampilkan halaman welcome untuk pertama kali
    welcome_message();

    while (1) {
        // Menggunakan readline untuk membaca input
        input = readline(show_prompt());
        
        // Menangani EOF (Ctrl+D)
        if (input == NULL) {
            printf("\n");
            break;
        }
        
        // Jika input tidak kosong
        if (strlen(input) > 0) {
            // Tambahkan ke history
            add_to_history(input);
            
            // Buat salinan input untuk dimodifikasi
            strncpy(input_copy, input, MAX_CMD_LEN - 1);
            input_copy[MAX_CMD_LEN - 1] = '\0';
            
            // Periksa apakah perintah mengandung pipeline atau bukan
            if (strchr(input_copy, '|') != NULL) {
                execute_pipeline(input_copy);
            } else {
                parse_input(input_copy, args);
                handle_redirection(args);
                execute_command(args);
            }
        }
        
        // Bebaskan memori dari readline
        free(input);
    }

    return 0;
}
