İstanbul Efsaneleri: Lale Savaşçıları 64bit @VER@ - Windows
(Windows 10 / 11, 64 bit)

==== TÜRKÇE ====================================================================

Bu paket yalnızca oyunun programıdır; özgün oyunun dosyalarını içermez.
Program, oyunu sizin elinizdeki kopyadan alır:

  * oyun CD'si (sürücüde), ya da
  * CD'nin disk kalıbı: .cue + .bin, .iso ya da .chd, ya da
  * oyunun 2005 sürümü: .zip, .7z ya da .rar arşivi, ya da açılmış klasörü.

Kurulum
  1. Bu klasörü dilediğiniz, yazılabilir bir yere açın (Program Files olmasın).
  2. Lale64.exe'yi çalıştırın.
  3. Program oyunu kendisi bulamazsa sorar: "Dosya Seç…" ile disk kalıbını
     (.cue / .iso / .chd) ya da arşivi, "Klasör Seç…" ile oyunun klasörünü
     seçin.  Kaynağı Lale64.exe'nin üzerine sürükleyip bırakmak ya da
     Lale64.exe'nin yanına koymak da yeterlidir.
  4. Aktarma bir kez yapılır.  CD ya da disk kalıbı, Lale64.exe'nin yanındaki
     DATA klasörüne LALE.CHD olarak yazılır; 2005 sürümünün dosyaları DATA
     klasörüne kopyalanır.  Bundan sonra kaynağa gerek kalmaz.

Klasörler (hepsi Lale64.exe'nin yanında)
  DATA    içe aktarılan oyun
  SAVE    oyunun yazdığı her şey: kayıtlı oyunlar, ayarlar
  VIDEO   isteğe bağlı: 4K videolar

Sürümler
  CD'den ya da ses izleri olan bir disk kalıbından kurulan oyun 1996 CD sürümü
  olarak çalışır (müzik CD'nin ses izlerinden).  Dosyalardan kurulan oyun 2005
  sürümü olarak çalışır.  lale.ini içinde "edition = cd" ya da "edition = 2005"
  ile kendiniz seçebilirsiniz (lale.ini.example dosyasına bakın).
  Level dergisinin 1998 CD'sinde ses izi yoktur; o CD'den kurulan oyun 2005
  sürümü olarak, müzikle çalışır ("edition = cd": diskteki 1996 programı,
  giriş ve menü müziği olmadan).

4K videolar
  ModDB'deki "Lale Savascilari 4K Video Pack" paketini indirin, içindeki VIDEO
  klasörünü Lale64.exe'nin yanına koyun:
  https://www.moddb.com/mods/lale-savascilari-4k-video-pack1
  Oyun sırasında Ctrl+V, 4K ile orijinal video arasında geçiş yapar; her
  geçişte sağ üst köşede üç saniye "4K Video" ya da "Orijinal Video" yazar.
  Oyunun kendi sesi korunur; yalnızca görüntü değişir.

Tam ekran: Alt+Enter, tam ekran ile pencere arasında geçiş yapar.

Program penceresi ve iletişim kutuları Türkçedir ve Windows'un açık/koyu
temasına uyar.  "Hakkında" kutusu: pencere başlığındaki simgeye tıklayın (ya
da Alt+Boşluk) > Hakkında….

Kaynak kod ve belgeler: https://github.com/Buyukcaglar/Lale-Savascilari

Bu program SDL3'ü (https://libsdl.org, zlib lisansı) kullanır; SDL3.dll bu
paketin parçasıdır.  Lisansların asıl metinleri: LICENSES.txt.

==== ENGLISH ===================================================================

This package is the game's program only; it does not contain the files of the
original game.  The program takes the game from your own copy:

  * the game CD (in a drive), or
  * a disc image of the CD: .cue + .bin, .iso or .chd, or
  * the 2005 release of the game: a .zip, .7z or .rar archive, or its unpacked
    folder.

Installing
  1. Unpack this folder to any place you can write to (not Program Files).
  2. Start Lale64.exe.
  3. If the program does not find the game itself, it asks (in Turkish):
     "Dosya Seç…" (choose a file) for the disc image (.cue / .iso / .chd) or
     the archive, "Klasör Seç…" (choose a folder) for the game's folder.  Dropping the source onto Lale64.exe, or putting it
     beside Lale64.exe, works as well.
  4. The import happens once.  A CD or disc image is written to the folder
     DATA beside Lale64.exe as LALE.CHD; the files of the 2005 release are
     copied to DATA.  The source is not needed afterwards.

Folders (all beside Lale64.exe)
  DATA    the imported game
  SAVE    everything the game writes: saved games, settings
  VIDEO   optional: 4K videos

Editions
  A game installed from the CD, or from a disc image with audio tracks, runs as
  the 1996 CD edition (music from the CD's audio tracks).  A game installed
  from files runs as the 2005 edition.  "edition = cd" or "edition = 2005" in
  lale.ini decides otherwise (see lale.ini.example).
  The Level magazine's 1998 CD has no audio tracks; a game installed from it
  runs as the 2005 edition, with music ("edition = cd": the disc's 1996
  program, without intro and menu music).

4K videos
  Download the "Lale Savascilari 4K Video Pack" from ModDB and put its VIDEO
  folder beside Lale64.exe:
  https://www.moddb.com/mods/lale-savascilari-4k-video-pack1
  In the game, Ctrl+V switches between 4K and the original video; "4K Video"
  or "Orijinal Video" appears in the top right corner for three seconds at
  each switch.  The game's own sound is kept; only the picture changes.

Source code and documentation: https://github.com/Buyukcaglar/Lale-Savascilari

Full screen: Alt+Enter switches between full screen and the window.

The program's window and dialogs are in Turkish and follow Windows' light or
dark mode.  The About box: click the icon in the window's title bar (or
Alt+Space) > Hakkında….

This program uses SDL3 (https://libsdl.org), zlib licence; SDL3.dll is part of
this package.  The licences in full: LICENSES.txt.
