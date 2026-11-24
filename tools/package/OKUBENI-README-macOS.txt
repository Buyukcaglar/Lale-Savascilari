İstanbul Efsaneleri: Lale Savaşçıları 64bit @VER@ - macOS
(macOS 11 ya da sonrası, Apple Silicon ve Intel / macOS 11 or later, Apple Silicon and Intel)

==== TÜRKÇE ====================================================================

Bu paket yalnızca oyunun programıdır; özgün oyunun dosyalarını içermez.
Program, oyunu sizin elinizdeki kopyadan alır:

  * CD'nin disk kalıbı: .cue + .bin, .iso ya da .chd, ya da
  * oyunun 2005 sürümü: .zip, .7z ya da .rar arşivi, ya da açılmış klasörü.

(Mac'te CD doğrudan sürücüden okunmaz; önce CD'nin disk kalıbını alın.)

Kurulum
  1. Bu klasörü dilediğiniz, yazılabilir bir yere açın (Programlar klasörü
     olmasın; oyun, Lale64.app'in yanına yazar).
  2. İlk açılış: uygulama Apple tarafından onaylanmış (notarised) değildir.
     Lale64.app'e sağ tıklayıp Aç'ı seçin (bir kez), ya da Terminal'de:
       xattr -dr com.apple.quarantine Lale64.app
  3. Program oyunu kendisi bulamazsa sorar: "Dosya Seç…" ile disk kalıbını
     (.cue / .iso / .chd) ya da arşivi, "Klasör Seç…" ile oyunun klasörünü
     seçin.  Kaynağı Lale64.app'in yanına koymak da yeterlidir.
  4. Aktarma bir kez yapılır.  Disk kalıbı, Lale64.app'in yanındaki DATA
     klasörüne LALE.CHD olarak yazılır; 2005 sürümünün dosyaları DATA
     klasörüne kopyalanır.  Bundan sonra kaynağa gerek kalmaz.

Klasörler (hepsi Lale64.app'in yanında)
  DATA    içe aktarılan oyun
  SAVE    oyunun yazdığı her şey: kayıtlı oyunlar, ayarlar
  VIDEO   isteğe bağlı: 4K videolar

Sürümler
  Ses izleri olan bir disk kalıbından kurulan oyun 1996 CD sürümü olarak
  çalışır (müzik CD'nin ses izlerinden).  Dosyalardan kurulan oyun 2005
  sürümü olarak çalışır.  lale.ini içinde "edition = cd" ya da
  "edition = 2005" ile kendiniz seçebilirsiniz (lale.ini.example dosyasına
  bakın).

4K videolar
  ModDB'deki "Lale Savascilari 4K Video Pack" paketini indirin, içindeki VIDEO
  klasörünü Lale64.app'in yanına koyun:
  https://www.moddb.com/mods/lale-savascilari-4k-video-pack
  Oyun sırasında Ctrl+V, 4K ile orijinal video arasında geçiş yapar.

Tuşlar
  PC'nin Alt tuşu SOL Option tuşudur.  Cmd+Return ya da Cmd+F: tam ekran.
  Cmd+Q: çıkış.  Cmd+. PC'nin Ctrl-Break'idir.  "Hakkında" kutusu: Lale64
  menüsü > "Lale64 Hakkında".

Kaynak kod ve belgeler: https://github.com/Buyukcaglar/Lale-Savascilari

Bu program SDL3'ü (https://libsdl.org, zlib lisansı) kullanır; SDL3
programın içine bağlanmıştır.  Lisansların asıl metinleri: LICENSES.txt.

==== ENGLISH ===================================================================

This package is the game's program only; it does not contain the files of the
original game.  The program takes the game from your own copy:

  * a disc image of the CD: .cue + .bin, .iso or .chd, or
  * the 2005 release of the game: a .zip, .7z or .rar archive, or its unpacked
    folder.

(On the Mac the CD is not read from the drive directly; make a disc image of
it first.)

Installing
  1. Unpack this folder to any place you can write to (not Applications; the
     game writes beside Lale64.app).
  2. First start: the app is not notarised by Apple.  Right-click Lale64.app
     and choose Open (once), or in Terminal:
       xattr -dr com.apple.quarantine Lale64.app
  3. If the program does not find the game itself, it asks (in Turkish):
     "Dosya Seç…" (choose a file) for the disc image (.cue / .iso / .chd) or
     the archive, "Klasör Seç…" (choose a folder) for the game's folder.
     Putting the source beside Lale64.app works as well.
  4. The import happens once.  A disc image is written to the folder DATA
     beside Lale64.app as LALE.CHD; the files of the 2005 release are copied
     to DATA.  The source is not needed afterwards.

Folders (all beside Lale64.app)
  DATA    the imported game
  SAVE    everything the game writes: saved games, settings
  VIDEO   optional: 4K videos

Editions
  A game installed from a disc image with audio tracks runs as the 1996 CD
  edition (music from the CD's audio tracks).  A game installed from files
  runs as the 2005 edition.  "edition = cd" or "edition = 2005" in lale.ini
  decides otherwise (see lale.ini.example).

4K videos
  Download the "Lale Savascilari 4K Video Pack" from ModDB and put its VIDEO
  folder beside Lale64.app:
  https://www.moddb.com/mods/lale-savascilari-4k-video-pack
  In the game, Ctrl+V switches between 4K and the original video.

Keys
  The PC's Alt is the LEFT Option key.  Cmd+Return or Cmd+F: full screen.
  Cmd+Q: quit.  Cmd+. is the PC's Ctrl-Break.  The About box: the Lale64 menu
  > "Lale64 Hakkında" (About Lale64).

Source code and documentation: https://github.com/Buyukcaglar/Lale-Savascilari

This program uses SDL3 (https://libsdl.org), zlib licence; SDL3 is linked into
the program.  The licences in full: LICENSES.txt.
