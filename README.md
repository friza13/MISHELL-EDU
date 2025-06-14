# MISHELL-EDU
Mishell-EDU: Shell Terminal Cerdas dengan Integrasi AI
Mishell-EDU adalah sebuah proyek custom shell yang ditulis dalam bahasa C untuk lingkungan Linux. Proyek ini bukan sekadar pengganti shell biasa, tetapi sebuah alat bantu canggih yang dirancang untuk mempermudah interaksi pengguna dengan terminal. Dilengkapi dengan integrasi Google Gemini AI, Mishell-EDU mampu menerjemahkan perintah bahasa manusia menjadi perintah bash yang siap dieksekusi.

Selain fitur AI, shell ini juga menyertakan berbagai perintah internal yang bermanfaat untuk monitoring sistem, manajemen file, dan konfigurasi jaringan, menjadikannya alat yang ideal untuk tujuan edukasi dan produktivitas.

Fitur Utama
Prompt Interaktif: Menampilkan direktori kerja saat ini dalam prompt, memberikan konteks yang jelas bagi pengguna.

Perintah Internal: Mendukung perintah dasar shell seperti cd, pwd, history, ls, cat, rm, mkdir, dan lainnya.

Integrasi Google Gemini AI:

ai setup: Untuk mengonfigurasi API Key Google Gemini Anda.

ai <pertanyaan>: Bertanya kepada AI untuk mendapatkan perintah bash berdasarkan deskripsi bahasa natural (misalnya: ai hapus file bernama "tugas lama.txt").

ai logout: Menghapus API key yang tersimpan dengan aman.

Monitoring Sistem:

cek cpu: Menampilkan penggunaan CPU secara real-time.

cek ram: Menampilkan statistik penggunaan memori (RAM).

cek disk: Menampilkan penggunaan partisi disk.

cek battery: Memeriksa status kapasitas baterai laptop.

Utilitas Jaringan:

test speed: Menguji kecepatan internet menggunakan speedtest-cli.

setup dns: Skrip otomatis untuk mengonfigurasi server DNS menggunakan BIND9 dengan cepat.

Dukungan Pipeline dan Redirection: Mampu mengeksekusi perintah kompleks menggunakan pipe (|) serta input/output redirection (<, >).

Manajemen Riwayat Perintah: Menyimpan dan menampilkan riwayat perintah yang telah dieksekusi.

Prasyarat
Sebelum melakukan instalasi, pastikan sistem Linux Anda (direkomendasikan Debian/Ubuntu) memiliki paket-paket berikut:

GCC Compiler: Untuk mengompilasi kode sumber C.

Make: Utilitas untuk otomatisasi kompilasi.

libcurl: Pustaka untuk melakukan permintaan HTTP (diperlukan untuk Gemini API).

libreadline: Pustaka untuk membaca input baris dengan fitur editing dan history.

Git: Untuk mengkloning repositori.

(Opsional) speedtest-cli: Untuk fitur test speed. Skrip akan mencoba menginstalnya secara otomatis jika belum ada.

(Opsional) bind9: Untuk fitur setup dns. Skrip akan mencoba menginstalnya secara otomatis jika belum ada.

Panduan Instalasi
Ikuti langkah-langkah berikut untuk menginstal dan menjalankan Mishell-EDU.

1. Buka Terminal Anda

2. Instal Dependensi yang Diperlukan

Jalankan perintah berikut untuk menginstal semua paket yang dibutuhkan:

sudo apt-get update
sudo apt-get install -y gcc make libcurl4-openssl-dev libreadline-dev git

3. Kloning Repositori

Gunakan git untuk mengunduh kode sumber dari repositori GitHub Anda.

# Ganti <URL_REPOSITORI_ANDA> dengan URL GitHub Anda yang sebenarnya
git clone <URL_REPOSITORI_ANDA>
cd <NAMA_DIREKTORI_REPOSITORI>

4. Kompilasi Program

Kompilasi file mishell.c menjadi sebuah executable bernama mishell menggunakan perintah di bawah ini:

gcc -o mishell mishell.c -lcurl -lreadline

Jika kompilasi berhasil, Anda akan menemukan sebuah file bernama mishell di dalam direktori.

Cara Menjalankan
Setelah kompilasi selesai, Anda dapat menjalankan shell dengan perintah berikut:

./mishell

Anda akan disambut dengan pesan selamat datang dan prompt mishell-EDU yang siap menerima perintah.

               /$$           /$$                 /$$      /$$$$$$$$  /$$$$$$$   /$$   /$$
              |__/          | $$                | $$     |_____ $$/ | $$____  \| $$  | $$
 /$$$$$$/$$$$  /$$  /$$$$$$$| $$$$$$$   /$$$$$$ | $$          /$$/ | $$    \ \| $$  | $$
| $$_  $$_  $$| $$ /$$_____/| $$__  $$ /$$__  $$| $$ /$$$$$$ /$$/ / | $$$$$$$$\| $$  | $$
| $$ \ $$ \ $$| $$|  $$$$$$ | $$  \ $$| $$$$$$$$| $$|______//$$/ / | $$____ / | $$  | $$
| $$ | $$ | $$| $$ \____  $$| $$  | $$| $$_____/| $$       /$$/ /  | $$      | $$  | $$
| $$ | $$ | $$| $$ /$$$$$$$/| $$  | $$|  $$$$$$$| $$      /$$/ /   | $$      |  $$$$$$/
|__/ |__/ |__/|__/|_______/ |__/  |__/ \_______/|__/     |__/ /    |__/       \______/
...
Silakan masukkan perintah!
mishell-EDU [/path/to/your/directory]>

Menggunakan Fitur AI (Google Gemini)
Untuk menggunakan fitur berbasis AI, Anda perlu mengatur API Key dari Google Gemini terlebih dahulu.

1. Dapatkan API Key

Kunjungi Google AI Studio.

Masuk dengan akun Google Anda.

Klik tombol "Get API key" untuk membuat API key baru. Salin kunci tersebut.

2. Konfigurasi di Mishell

Jalankan perintah ai setup di dalam mishell.

Masukkan API key yang sudah Anda salin saat diminta.

mishell-EDU [~]> ai setup
Masukkan Google Gemini API Key Anda: <PASTE_API_KEY_ANDA_DI_SINI>
API key berhasil diatur.
API key telah disimpan secara otomatis

3. Mulai Bertanya pada AI

Sekarang Anda bisa menggunakan perintah ai untuk meminta bantuan. Mishell akan memberikan saran perintah bash, lalu menanyakan apakah Anda ingin menjalankannya.

Contoh Penggunaan:

# Contoh 1: Menghapus beberapa file dengan pola tertentu
mishell-EDU [~/test]> ai hapus semua file yang berakhiran .log

==== Respons dari Gemini AI ====
rm ./*.log
================================

Perintah yang akan dijalankan: rm ./*.log
Apakah Anda ingin menjalankan perintah ini? (y/n): y
Menjalankan perintah...
```bash
# Contoh 2: Membuat direktori dan pindah ke dalamnya
mishell-EDU [~]> ai buat folder baru bernama 'proyek-penting' dan masuk ke dalamnya

==== Respons dari Gemini AI ====
mkdir 'proyek-penting' && cd 'proyek-penting'
================================

Perintah yang akan dijalankan: mkdir 'proyek-penting' && cd 'proyek-penting'
Apakah Anda ingin menjalankan perintah ini? (y/n): y
Menjalankan perintah...
mishell-EDU [~/proyek-penting]>

Untuk keluar dari Mishell, cukup ketik q atau tekan Ctrl+D.
