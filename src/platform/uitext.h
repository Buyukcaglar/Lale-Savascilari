/*
 * uitext.h - every text the port itself shows the player, in Turkish (the
 * user's request of 2026-11-25: all dialog and window text in Turkish only).
 *
 * These are the port's own texts - start-up, import, errors, the About box -
 * not the game's, which come from the original program and its data.  The
 * diagnostic log on the console (SDL_Log) stays English.
 *
 * This is the one source file with letters outside ASCII: it is UTF-8, as
 * the strings must be at run time (SDL, Windows through the UTF-8 code page
 * of lale.manifest, Cocoa).  Messages built with seterr() come out as
 * "<text>: <file or detail>".
 */
#ifndef LALE_PLATFORM_UITEXT_H
#define LALE_PLATFORM_UITEXT_H

/* ---- windows and buttons ---------------------------------------------------------- */
#define T_TITLE             "İstanbul Efsaneleri - Lale Savaşçıları"
#define T_OK                "Tamam"
#define T_CANCEL            "İptal"
#define T_QUIT              "Çıkış"
#define T_CHOOSE_FILE       "Dosya Seç…"
#define T_CHOOSE_FOLDER     "Klasör Seç…"
#define T_PICK_FILE_TITLE   "Oyunun disk kalıbını ya da arşivini seçin"
#define T_PICK_FOLDER_TITLE "Oyunun klasörünü seçin"
#define T_PICK_ACCEPT       "Seç"
#define T_FILTER_GAME       "Oyunun disk kalıpları ve arşivleri"
#define T_FILTER_ALL        "Tüm dosyalar"

/* ---- start-up (setup.c) ------------------------------------------------------------ */
#define T_ASK_SOURCE \
    "Bu program, özgün oyunun sizdeki kopyasına ihtiyaç duyar. Şunlardan birini kullanabilir:\n\n" \
    "•  Oyun CD'si: CD'yi sürücüye takıp programı yeniden başlatın ya da CD'nin disk kalıbını " \
    "seçin (.bin dosyalarıyla birlikte .cue, ya da .iso, .chd).\n\n" \
    "•  2005 sürümünün dosyaları: .zip, .7z ya da .rar arşivi ya da arşivin açıldığı klasör.\n\n" \
    "Oyun, bu programın yanındaki DATA klasörüne kopyalanır; kayıtlı oyunlar SAVE klasörüne yazılır."
#define T_NO_FOLDERS_FMT    "DATA ve SAVE klasörleri burada oluşturulamıyor:\n%s"
#define T_CANNOT_USE_FMT    "Bu kaynak kullanılamıyor:\n%s\n\n%s"
#define T_CANNOT_USE_PICK_FMT \
    "Bu kaynak kullanılamıyor:\n%s\n\n%s\n\n" \
    "Oyun CD'sinin disk kalıbını ya da 2005 sürümünün dosyalarını seçin."
#define T_BAD_CHD_FMT \
    "Bu dosya kullanılamıyor:\n%s\n\n%s\n\nDosyayı silin ve oyun CD'sini programa yeniden verin."
#define T_CD_EDITION_NEEDS_DISC \
    "CD sürümü için, ses izleriyle birlikte oyun CD'si gerekir (DATA/LALE.CHD).\n\n" \
    "Programı CD sürücüdeyken ya da CD'nin disk kalıbıyla (.cue) başlatın ya da " \
    "edition = 2005 ayarını kullanın."

/* ---- import (install.c): what the progress window says ------------------------------ */
#define T_P_COPY_FILES      "Oyun dosyaları DATA klasörüne kopyalanıyor"
#define T_P_WRITE_CHD       "Disk DATA/LALE.CHD dosyasına yazılıyor"
#define T_P_CHECK_DATA_CHD  "DATA/LALE.CHD denetleniyor"
#define T_P_CHECK_CHD       "CHD dosyası denetleniyor"
#define T_P_COPY_CHD        "CHD dosyası DATA klasörüne kopyalanıyor"
#define T_P_UNPACK          "Arşiv açılıyor"

/* ---- errors: files and folders ------------------------------------------------------- */
#define T_E_READ            "Okunamıyor"
#define T_E_CREATE          "Oluşturulamıyor"
#define T_E_WRITE           "Yazılamıyor"
#define T_E_WRITE_FULL      "Yazılamıyor (disk dolu olabilir)"
#define T_E_OPEN            "Açılamıyor"
#define T_E_READ_FOLDER     "Klasör okunamıyor"
#define T_E_CREATE_FOLDER   "Klasör oluşturulamıyor"
#define T_E_NO_SUCH         "Böyle bir dosya ya da klasör yok"

/* ---- errors: the import ---------------------------------------------------------------- */
#define T_E_INCOMPLETE      "Oyun eksik (3D.CKD, 0GFL.CKD, TRDOS.DAT ve diğer dosyalar gerekli)"
#define T_E_NOT_GAME_CD     "Bu, oyunun CD'si değil (içinde 3D.CKD yok)"
#define T_E_BAD_SECTORS_FMT "Veri izinin %u sektörü okunamadı (disk kirli ya da çizik olabilir)"
#define T_E_READ_IMAGE      "Disk kalıbı okunamıyor"
#define T_E_NOTHING_HERE    "Burada ne oyunun dosyaları (3D.CKD ve diğerleri) ne de bir disk kalıbı (.cue, .chd, .iso) var"
#define T_E_UNPACK          "Arşiv açılamadı (arşivi kendiniz açıp klasörünü gösterin)"
#define T_E_BIN_NEEDS_CUE   ".bin kalıbının yanında .cue dosyası da olmalı"

/* ---- errors: disc images (cdrom.c, chd.c) ---------------------------------------------- */
#define T_E_CUE_OPEN        "Cue dosyası açılamıyor"
#define T_E_CUE_FILE_LINE   "Cue dosyasında hatalı FILE satırı"
#define T_E_CUE_BINARY      "Cue dosyasında yalnızca BINARY dosyalar destekleniyor"
#define T_E_CUE_MANY_FILES  "Cue dosyasında çok fazla dosya var"
#define T_E_TRACK_OPEN      "İz dosyası açılamıyor"
#define T_E_TRACK_SIZE      "İz dosyasının boyutu alınamıyor"
#define T_E_CUE_TRACK_LINE  "Cue dosyasında hatalı TRACK satırı"
#define T_E_TRACK_TYPE      "Desteklenmeyen iz türü"
#define T_E_CUE_NO_TRACKS   "Cue dosyasında iz yok"
#define T_E_DISC_LAYOUT     "Disk düzeni hatalı"
#define T_E_CHD_TRACK_META  "CHD içindeki iz bilgisi hatalı"
#define T_E_CHD_NOT_CD      "CD kalıbı değil (iz bilgisi yok)"
#define T_E_CHD_NOT_CHD     "CHD dosyası değil"
#define T_E_CHD_VERSION     "Yalnızca CHD sürüm 5 destekleniyor"
#define T_E_CHD_CODEC       "Desteklenmeyen CHD sıkıştırması"
#define T_E_CHD_PARENT      "Üst dosyaya bağlı CHD desteklenmiyor"
#define T_E_CHD_NOT_CD_SIZE "CD kalıbı değil (blok ya da birim boyutu uymuyor)"
#define T_E_CHD_MAP         "CHD bozuk (blok haritası)"
#define T_E_CHD_HUNK        "CHD bozuk: blok"
#define T_E_CHD_SHA_DATA    "CHD bozuk: verinin SHA-1 değeri başlıktakiyle uyuşmuyor"
#define T_E_CHD_SHA_ALL     "CHD bozuk: genel SHA-1 değeri başlıktakiyle uyuşmuyor"

/* ---- errors: ZIP archives (zipfile.c) ---------------------------------------------------- */
#define T_E_ZIP_NOT_ZIP     "ZIP arşivi değil"
#define T_E_ZIP_ZIP64       "ZIP64 arşivleri desteklenmiyor"
#define T_E_ZIP_DAMAGED     "ZIP arşivi bozuk"
#define T_E_ZIP_METHOD      "ZIP içindeki dosya şifreli ya da desteklenmeyen bir yöntemle sıkıştırılmış"
#define T_E_ZIP_DEFLATE     "ZIP arşivi bozuk (sıkıştırılmış veri)"
#define T_E_ZIP_CRC         "ZIP arşivi bozuk (CRC-32 ya da boyut uyuşmuyor)"

/* ---- errors: a CD drive (drive.c) ---------------------------------------------------------- */
#define T_E_NOT_DRIVE       "Sürücü değil"
#define T_E_DRIVE_OPEN      "Sürücü açılamıyor"
#define T_E_DRIVE_NO_DISC   "Sürücüde disk yok ya da diskin içindekiler tablosu okunamıyor"
#define T_E_DRIVE_NOT_HERE  "Bu sistemde disk doğrudan sürücüden okunamıyor"

/* ---- the About box (about.c) ------------------------------------------------------------------ */
#define T_ABOUT_MENU        "Hakkında…"
#define T_ABOUT_MENU_MAC    "Lale64 Hakkında"
#define T_ABOUT_TITLE_FMT   "İstanbul Efsaneleri: Lale Savaşçıları 64bit v" /* + version + */
#define T_ABOUT_TITLE_END   " Hakkında"
#define T_ABOUT_LINE1       "64 bit sürüm: Onur BUYUKCAGLAR"
#define T_ABOUT_LINE2       "Anthropic Claude yapay zekâ desteğiyle"
#define T_ABOUT_LICENSES    "Lisanslar:"

#define T_SDL_AS_WIN        "değiştirilmeden SDL3.dll olarak birlikte verilir"
#define T_SDL_AS_MAC        "değiştirilmeden, uygulamanın içinde yer alır"
#define T_SDL_AS_OTHER      "değiştirilmeden kullanılır"

/* The licences in Turkish; their binding English texts are docs/LICENSES.txt
   (LICENSES.txt in the packages).  T_LICENSES_1 <how SDL ships> T_LICENSES_2 */
#define T_LICENSES_1 \
    "MIT Lisansı\n\n" \
    "Telif hakkı (c) 2026 Onur BUYUKCAGLAR\n\n" \
    "Bu yazılımın ve ilgili belge dosyalarının (\"Yazılım\") bir kopyasını edinen herkese, " \
    "Yazılım'ı hiçbir kısıtlama olmaksızın kullanma izni ücretsiz olarak verilir; buna " \
    "Yazılım'ın kopyalarını kullanma, kopyalama, değiştirme, birleştirme, yayımlama, dağıtma, " \
    "alt lisanslama ve/veya satma hakları ile Yazılım'ın verildiği kişilere de aynı izni " \
    "tanıma hakkı, sınırlama olmaksızın dahildir. Bu izin şu koşula bağlıdır:\n\n" \
    "Yukarıdaki telif hakkı bildirimi ve bu izin bildirimi, Yazılım'ın bütün kopyalarına ya " \
    "da önemli bölümlerine eklenmelidir.\n\n" \
    "YAZILIM, TİCARİ ELVERİŞLİLİK, BELİRLİ BİR AMACA UYGUNLUK VE HAK İHLALİ BULUNMAMASI " \
    "GARANTİLERİ DAHİL ANCAK BUNLARLA SINIRLI OLMAMAK ÜZERE, AÇIK YA DA ZIMNİ HİÇBİR GARANTİ " \
    "VERİLMEKSİZİN \"OLDUĞU GİBİ\" SAĞLANIR. YAZARLAR YA DA TELİF HAKKI SAHİPLERİ; İSTER " \
    "SÖZLEŞMEDEN, İSTER HAKSIZ FİİLDEN, İSTER BAŞKA BİR NEDENDEN DOĞSUN, YAZILIM'DAN, " \
    "YAZILIM'IN KULLANILMASINDAN YA DA YAZILIM'LA İLGİLİ BAŞKA İŞLEMLERDEN KAYNAKLANAN HİÇBİR " \
    "TALEP, ZARAR YA DA BAŞKA YÜKÜMLÜLÜKTEN SORUMLU TUTULAMAZ.\n\n" \
    "Lisans, bu sürümün kendi kodunu kapsar. İstanbul Efsaneleri: Lale Savaşçıları, programı " \
    "ve verileri sahiplerine aittir; bu yazılımın parçası değildir. Yazılımın çalışması için " \
    "özgün oyunun bir kopyası gerekir.\n\n" \
    "Üçüncü taraf yazılımlar\n\n" \
    "Bu program SDL 3'ü (Simple DirectMedia Layer, https://libsdl.org) kullanır; SDL, "
#define T_LICENSES_2 \
    " ve zlib lisansı altındadır:\n\n" \
    "Telif hakkı (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>\n\n" \
    "Bu yazılım, açık ya da zımni hiçbir garanti verilmeksizin \"olduğu gibi\" sağlanır. " \
    "Yazarlar, bu yazılımın kullanılmasından doğan hiçbir zarardan sorumlu tutulamaz.\n\n" \
    "Bu yazılımı ticari uygulamalar dahil her amaçla kullanma, değiştirme ve serbestçe " \
    "yeniden dağıtma izni, aşağıdaki kısıtlamalara uyulması koşuluyla herkese verilir:\n\n" \
    "1. Bu yazılımın kaynağı yanlış tanıtılamaz; özgün yazılımı sizin yazdığınızı öne " \
    "süremezsiniz. Bu yazılımı bir üründe kullanırsanız ürün belgelerinde teşekkür etmeniz " \
    "memnuniyetle karşılanır, ancak zorunlu değildir.\n" \
    "2. Değiştirilmiş kaynak sürümleri açıkça öyle olduklarını belirtmeli, özgün yazılımmış " \
    "gibi tanıtılmamalıdır.\n" \
    "3. Bu bildirim hiçbir kaynak dağıtımından kaldırılamaz ve değiştirilemez.\n\n" \
    "Bu metinler bilgi amaçlı Türkçe çevirilerdir. Lisansların bağlayıcı asıl metinleri " \
    "İngilizcedir: paketteki LICENSES.txt dosyasında ve kaynak deposunda (docs/LICENSES.txt) " \
    "bulunur.\n"

#endif
