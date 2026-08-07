# stellar-L3N-etag — contesto di sviluppo

Fork di firmware alternativo (ispirato ad ATC_TLSR_Paper) per tag e-paper elettronici,
trasformati in orologio da tavolo con calendario, grafico temperatura, generatore di
testi "blasfemi" (`smadonnator.c`), e opzionalmente tag Apple Find My / BLE generico.

## Hardware

- **SoC**: Telink TLSR8258 (arch tc32), SDK Telink BLE. Firmware bare-metal:
  `while(1) { main_loop(); }` in `main.c` — **non** un vero RTOS, anche se il
  componente FreeRTOS è linkato (nessun task/scheduler viene mai avviato: se serve
  sapere "stato di FreeRTOS", la risposta è che non è in uso).
- **Display**: confermato via datasheet (`docs/HINK-E0213A200-A0_Spec_bwr.pdf`, si
  trova sul branch `nebular296x400`, non su `main`) = **Holitech HINK-E0213A200**,
  controller **SSD1680**, nativo 122×250px (source×gate) → orientamento landscape
  usato dal firmware: **250×128** (122 reali + padding per byte pieno), **3 colori
  B/W/R**.
- **UART**: TX=PB1, RX=PA0, 115200 8N1. Il pin fisico sulla scheda non è ancora
  stato individuato dall'utente — l'unico canale di debug funzionante finora è il
  log diagnostico via BLE (vedi sotto).

## Vincolo critico: RAM di retention

Il chip trattiene solo i **32K bassi di SRAM** attraverso il deep-sleep (che è lo
stato normale tra un evento BLE e l'altro — il device dorme di continuo). Quello
stesso spazio da 32K è condiviso con `.ram_code` (funzioni marcate
`_attribute_ram_code_`), che da sole occupano già **~17KB**. Il margine libero
reale è nell'ordine di **poche centinaia di byte**, non "32K meno il retention
usato" come sembrerebbe leggendo solo `.retention_data`.

Prima di aggiungere qualunque variabile `RAM`/`_attribute_data_retention_`,
verificare con:
```
Firmware/tc32_windows/bin/tc32-elf-size.exe -A Firmware/out/ATC_Paper.elf
```
Se serve più margine, il candidato più probabile è **rimuovere
`_attribute_ram_code_`** da funzioni che non hanno vera necessità di girare da RAM
(timing critico, o chiamate durante operazioni su flash) — non tutte le funzioni
marcate così in `epd.c` sembrano averne realmente bisogno, ma non è stato ancora
verificato con cura quali.

I framebuffer di lavoro **non** necessitano di retention se vengono sempre
riscritti per intero in un solo ciclo sincrono (es. `epd_buffer_red`, il piano
rosso, usato solo da `epd_purge_tick()` e dall'upload BLE grezzo — non più da
nessuna scena, vedi sotto). Fa eccezione `epd_buffer`/`epd_temp`: quelli restano `RAM` perché
`epd_ble_service.c` li usa anche per l'upload immagine incrementale via BLE, a
pacchetti separati nel tempo — devono sopravvivere al sonno tra un pacchetto e
l'altro.

## Architettura firmware (`Firmware/src`)

- `main.c` — boot: distingue cold-start da risveglio-da-deep-retention
  (`pm_is_MCU_deepRetentionWakeup()`), arma il watchdog subito prima del
  `while(1)`.
- `app.c` — `main_loop()`: batteria/temperatura, advertising BLE, dispatch scena
  EPD, LED di stato, feed del watchdog.
- `epd.c` — orchestratore: tabella `epd_drivers[]` (dispatch per modello rilevato),
  le "scene" (funzioni di disegno), calendario/orologio/grafico temperatura.
- `epd_ssd168x.c/.h` — driver **unificato e parametrizzato** per tutta la
  famiglia di controller SSD1675/1680 (sostituisce i vecchi `epd_bwr_213.c`,
  `epd_bwr_154.c`, `epd_bwr_296.c`, ora eliminati). `epd_bw_213_ice.c` e
  `epd_bw_213.c` (controller UC8151, davvero diverso) restano driver separati
  dietro la stessa interfaccia a puntatori a funzione.
- `debug_log.c/.h` — ring buffer diagnostico da 1KB in RAM di retention,
  spento di default (costa cicli ad ogni `printf`), leggibile via BLE.
- `flash.c/.h` — persistenza impostazioni (incluso profilo EPD e stato del log).
- `cmd_parser.c` — protocollo comandi BLE su characteristic RxTx (un byte comando
  + parametri). Logga ogni comando **tranne** `0xE8`/`0xE9`/`0xEA` (gestione del
  log stesso — loggarli crea un loop che si autoalimenta).
- `web_tools/index.html` + `web_tools/js/` — interfaccia Web Bluetooth. **Va
  servita via HTTP** (es. `python -m http.server` dentro `web_tools/`), **mai
  aperta come `file://`**: Web Bluetooth richiede un "secure context" e i path
  relativi ai file JS falliscono silenziosamente da file locale.

## Bug reali trovati e risolti in questa sessione (utile in caso di regressioni)

1. Diverse scene disegnavano su un canvas fisso 296×128 invece di usare la
   risoluzione attiva rilevata (250×128) → corretto con `epd_res_width`/
   `epd_res_height` al posto delle macro `epd_width`/`epd_height` (quelle restano
   solo la capacità massima statica dei buffer).
2. `epd_ble_service.c` e `epd_display_tiff()` passavano ancora `epd_buffer_size`
   (macro, capacità massima) invece della risoluzione attiva a `EPD_Display()` →
   disallineava il protocollo SPI col pannello reale e bloccava il device su
   "Clear Screen"/upload immagini.
3. `printf()`/`puts()` erano **dead code** da tempo (`puts()` commentato in
   `u_printf.c`, quasi certamente apposta). Riattivati per il logging, ma
   `puts()` aveva un `while(uart_tx_is_busy())` **senza timeout** → poteva
   bloccare per sempre se nulla è collegato all'UART. Aggiunto timeout di 20ms/byte
   (`uart_wait_tx_done()` in `uart.c`).
4. Watchdog, due tentativi falliti prima di funzionare:
   - Armato troppo presto (prima che `user_init_normal()` completasse) → boot
     loop, perché nulla lo nutriva ancora.
   - Logica "spegni il feed se l'EPD resta busy troppo a lungo" tarata a 8s →
     un refresh completo del pannello BWR dura di più, causando reset ripetuti
     ad ogni boot (il primo refresh dopo boot è sempre completo). Quella logica
     è ora **disabilitata** (`WATCHDOG_EPD_STUCK_LIMIT_MS` in `app_config.h`,
     vedi sotto). Il watchdog attuale nutre incondizionatamente ogni giro di
     `main_loop()` — cattura solo hang veri della CPU.
5. `scaleTemp()` non sottraeva il minimo prima di scalare (bug di interpolazione).
6. `drawTempGraph()`: un clamp restringeva l'asse Y vicino alla temperatura
   istantanea, vanificando la scala storica richiesta; condizione invertita per
   decidere quando disegnare la banda storica min/max.
7. `drawCalendar()`: variabile `text_color` calcolata ma mai usata; bug
   `isHoliday` (nome funzione, sempre "vero") al posto di `is_holiday` (variabile).
8. `FixBuffer()` invertiva sempre i bit (corretto per il piano nero, polarità
   panel 1=bianco) ma la stessa inversione sul piano **rosso** (polarità 1=rosso,
   diretta) produceva l'esatto contrario. Ora prende un parametro `invert`
   (`true` per nero, `false` per rosso).
9. **Il parametro colore di `obdSetPixel`/`obdWriteStringCustom`/`obdRectangle`
   non seleziona rosso vs nero** — è solo on/off sul piano (`OBDISP*`) a cui lo
   passi. Per ottenere rosso serve disegnare su un **secondo piano/buffer
   separato** (pattern usato ancora oggi da `epd_purge_tick()`, non più da
   nessuna scena — vedi punto 10), poi passato come `red_image` a
   `EPD_Display()`.
10. **Il rosso non sopravvive a un refresh parziale su questo pannello.**
    Investigato a fondo (2026-08): la LUT custom di refresh parziale
    (`ssd168x_lut_partial` in `epd_ssd168x.c`, ereditata identica dai vecchi
    driver `epd_bwr_213/154/296.c`) ha, per analisi strutturale dei byte, un
    solo impulso di tensione breve seguito da tutti zeri — sufficiente per un
    flip bianco/nero bistabile veloce, ma troppo corto per muovere il
    pigmento rosso. **Tentativo fatto e fallito**: il datasheet SSD1680
    espone una seconda forma d'onda in OTP, "Display Mode 2" (cmd `0x22`
    codici `0xB9`/`0xCF`, mai usata prima), potenzialmente più adatta.
    Provata su hardware reale: il pannello smetteva di aggiornarsi
    visibilmente dopo alcuni minuti di refresh parziali — una regressione
    **peggiore** del semplice sbiadire del rosso. **Fatto rollback completo**
    a `ssd168x_init_common`/`EPD_SSD168x_Display` con solo Display Mode 1 +
    LUT custom per il parziale (stato pre-esperimento). **Non ritentare
    Display Mode 2 senza capire meglio perché smetteva di aggiornare** —
    possibile causa: la LUT/waveform di Mode 2 potrebbe essere calibrata per
    tutt'altro scopo (es. range di temperatura diverso, o richiede una
    sequenza di comandi diversa non documentata nella parte di datasheet
    letta finora) e non essere affatto un "partial update" nel senso atteso.
    **Stato attuale**: `epd_myScene` (scena 5, calendario) **non usa più il
    rosso** — festività/domeniche evidenziate solo sul piano nero. Il rosso
    resta usato solo da `epd_purge_tick()` (deep-clean, sempre a refresh
    completo) e dall'upload BLE grezzo del piano rosso
    (`epd_ble_service.c`), entrambi non affetti dal problema perché non
    passano mai per un refresh parziale.

## Cose note ma non ancora risolte

- Blocchi sporadici durante l'interazione BT: indagati parzialmente, il fix al
  timeout UART (#3 sopra) è un candidato forte ma non validato in modo
  definitivo come causa unica.
- `epd_display_tiff()` ha ancora dimensioni di decode hardcoded 250×122 a
  prescindere dal pannello realmente attivo.
- Le scene 1–4 (`epd_display`, `epd_display_info`, `epd_display_time_with_date`,
  `epd_display_my`) non hanno il piano rosso e usano coordinate pensate per un
  canvas 296px — non verificate a fondo su HINK213 (250px) come invece è stato
  fatto per `epd_myScene`.
- `WATCHDOG_EPD_STUCK_LIMIT_MS` esiste ma è disattivato: prima di riattivarlo
  serve una misura reale del tempo di refresh completo del pannello.
- Il buffer di log diagnostico è 1KB, non i 2KB inizialmente richiesti, per lo
  scarso margine di RAM di retention (vedi sopra).

## Come lavora l'utente (importante per non ripetere errori costosi)

- Builda e flasha l'hardware **lui stesso**; io non ho accesso diretto al
  device né a un log seriale live.
- **Nessun accesso UART fisico** finora: l'unico canale diagnostico è il log
  BLE (`debug_log.c` + comandi `0xE8`/`0xE9`/`0xEA` + sezione "Log Diagnostico"
  nell'interfaccia web).
- Un boot-loop o un hang serio può rendere il device **irraggiungibile via
  BLE**, costringendo a un reflash via hardware (non banale) — per questo
  qualunque modifica che tocca boot/watchdog/interrupt va introdotta con
  **cautela estrema** e spiegata chiaramente prima di procedere.
- Dopo un reflash conviene sempre **staccare e riattaccare la batteria**: il
  chip mantiene la RAM di retention anche attraverso un reflash via SWD, quindi
  senza un vero power-cycle si può ripartire da uno stato "sporco" che sembra un
  bug nuovo e invece è solo residuo del run precedente.
- Branch di lavoro: **`test`**. `main` è la baseline che funziona, da non
  toccare direttamente.
- L'utente ha talvolta **GitHub Desktop aperto in parallelo** al lavoro da riga
  di comando — è già successo che cambiasse branch/stashasse modifiche in
  concorrenza con `git` da terminale. Se lo stato del repo sembra incoerente,
  controllare `git reflog`/`git stash list` prima di assumere un errore proprio.
