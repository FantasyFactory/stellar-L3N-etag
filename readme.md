<h1 align="center">Firmware per cartellino elettronico dei prezzi/AirTag Hanshow Stellar L3N</h1>

### fork da https://github.com/reece15/stellar-L3N-etag

### Principali modifiche
- 1. Modificata funzione per far lampeggiare il led, ora supporta 7 colori: command 0x81 + color
  cmd_parser.c cmd_parser()
  led.c set_led_color()
- 2. aggiunta d 3 scene: 
  - command 0xe1 03 Uguale alla scena 1 ma tradotto
  - command 0xe1 04 Uguale alla scena 2 ma tradotto
  - command 0xe1 05 NUOVA SCENA
- 3. Nuova scena:
![Modalità orologio 5, calendario ](/images/PXL_20250122_003022747-EDIT.jpg)
  - Visualizza mese, anno, calendario (il giorno attuale è invertito)
  - Visualizza l'ora e il minuto
  - Visualizza la temperatura attuale
  - visualizza un grafico con le temperature del giorno: sull'asse orizzontale la giornata divisa in quarti d'ora (96px) sull'asse verticale una griglia con la temperatura massima, minima e corrente
  - visualizza in modo testuale le temperature massime e minime della giornata
  - visualizza in modo testuale le temperature massime e minime all'orario corrente del giorno precedente
  - visualizza il nome del device
  - visualizza la percentuale della batteria

### Python tool
```
EPaper BLE Controller /python_tools/myesl.py

options:
  -h, --help            show this help message and exit
  -scan                 Scan for EPaper devices
  -address ADDRESS      BLE device address
  -name NAME            BLE device name
  -setmode {0,1,2,3,4,5}
  -screenwhite          Set screen white
  -screenrefresh        Refresh screen
  -screenrefresh        Refresh screen
  -fullpicturerefresh   Full picture refresh
  -partialpicturerefresh
                        Partial picture refresh
  -uploadpicture UPLOADPICTURE
                        Upload picture file
  -dither {none,floydsteinberg,bayer,atkinson}
                        Dithering mode
  -setdatetime [SETDATETIME]
                        Set datetime with optional hour offset
  -blink {0,1,2,3,4,5,6,7}
                        Set LED color (1=Red,2=Green,3=Blue,4=Yellow,5=Aqua,6=Magenta,7=White,0=Off)
  -gettemp              Get temperature
  -getbattery           Get battery level
```
### segue readme.md originale
---------------------------------------------------------------------------------

### Modelli applicabili L3N@ (Nota: sono compatibili solo i dispositivi L3N@ da 2,9 pollici, altri modelli del progetto originale potrebbero non essere più compatibili)

### Risultato finale

- [Caricamento immagine sul Web](https://javabin.cn/stellar-L3N-etag/web_tools/)
 ![Gestione Bluetooth](/images/web.jpg)
- Modalità Orologio 2, Modalità Immagine
 ![Modalità orologio 2, modalità immagine](/images/1553702163.jpg)

![Modalità orologio 2, modalità immagine](/images/1587504241.jpg)

### Passaggi per flashare il firmware

- 1. Rimuovere il coperchio della batteria e verificare che la scheda madre sia come mostrato di seguito. (Oppure controlla se il controllo master è TLSR8359)

![Schema di saldatura](/USB_UART_Flashing_connection.jpg)

- 2. Saldare i quattro fili di GND, VCC, RX e RTS.
- 3. Utilizzare il modulo usb2ttl (CH340) per collegare i quattro fili saldati. Tra questi, rx è collegato a tx, tx è collegato a rx, vcc è collegato a 3,3 V e GND è collegato a GND. Collegare il filo volante RTS al terzo pin del chip CH340G (è anche possibile lasciarlo non saldato e collegarlo manualmente a GND prima della masterizzazione).
- 4. Aprire https://atc1441.github.io/ATC_TLSR_Paper_UART_Flasher.html, selezionare la velocità in baud predefinita 460800, l'Atime predefinito e il file Firmware/ATC_Paper.bin
- 5. Fare prima clic su Sblocca, poi su Scrivi per svuotare e attendere il completamento. Una volta completata l'operazione, lo schermo si aggiornerà automaticamente.

### Compilazione del progetto

```comando

 Firmware del CD
 makeit.exe pulito && makeit.exe -j12

```

Dopo il successo, richiedi il contenuto:

```
'Crea immagine Flash (formato binario)'
'Invocazione: TC32 crea elenco esteso'
'Invocazione: Dimensioni di stampa'
"tc32_windows\\bin\\"tc32-elf-size -t ./out/ATC_Paper.elf
copia da `./out/ATC_Paper.elf' [elf32-littletc32] a `./out/../ATC_Paper.bin' [binario]
 testo dati bss dec esadecimale nome file
 75608 4604 25341 105553 19c51 ./out/ATC_Paper.elf
 75608 4604 25341 105553 19c51 (TOTALI)
'Edificio finito: sizedummy'
' '
tl_fireware_tools.py v0.1 sviluppo
Firmware CRC32: 0xe62d501e
'Costruzione completata: out/../ATC_Paper.bin'
' '
'Edificio completato: out/ATC_Paper.lst'
' '
```

### Connessione Bluetooth e aggiornamento OTA

- 1. Per prima cosa è necessario scollegare la linea TTL TX, altrimenti la connessione Bluetooth non funzionerà.
- 2. Aggiornamento OTA: https://atc1441.github.io/ATC_TLSR_Paper_OTA_writing.html

### Carica le immagini

- 1. Eseguire `cd web_tools && python -m http.server`
- 2. Aprire http://127.0.0.1:8000 e connettere il Bluetooth sulla pagina
- 3. Seleziona l'immagine e caricala. Dopo il caricamento, puoi aggiungere testo o disegnare testo manualmente. È anche possibile impostare l'algoritmo di dithering.
- 4. Invia al dispositivo e attendi che lo schermo si aggiorni

### Connettiti alla rete findmy di Apple e simula airtag
- Il dispositivo supporta l'accesso alla rete findmy di Apple. (Il dispositivo invierà automaticamente una chiave pubblica conforme al protocollo airtag tramite trasmissione Bluetooth. Quando un dispositivo Apple vicino al dispositivo riceve la chiave pubblica, utilizzerà la chiave pubblica per crittografare il suo informazioni sulla posizione e quindi inviarle al server Findmy, gli utenti possono utilizzare la propria chiave privata per ottenere le informazioni sulla posizione del dispositivo dal server Apple)
- Questa funzione è disabilitata per impostazione predefinita
- Per abilitare questa funzione, è necessario modificare i dati dopo PUB_KEY= nel file ble.c e sostituirli con la propria chiave pubblica. Per informazioni su come ottenere PUB_KEY, fare riferimento al progetto (https://github.com/dchristl/macless-haystack o https://github.com/malmeloo/openhaystack)
- Per abilitare questa funzione, è necessario modificare anche il file ble.c AIR_TAG_OPEN=1

### Problemi risolti/irrisolti

- [X] Errore di compilazione
- [X] Il lampeggiamento non funziona
- [X] L'area dello schermo è errata/anormale
- [X] Impossibile connettersi al Bluetooth/Aggiornamento OTA Bluetooth
- [ ] Riconoscimento automatico dei modelli
- [X] Script di generazione di immagini Python
- [X] Risolto il problema della dimensione di visualizzazione errata durante l'invio di immagini tramite Bluetooth
- [X] Aggiungi notifica dopo aver caricato le immagini tramite Bluetooth
- [X] Aggiungere scene e supportare la commutazione
- [X] Modalità Immagine
- [X] Il Web supporta la commutazione delle immagini
- [X] Aggiungi nuove scene temporali
- [X] Supporta l'impostazione di anno, mese e giorno
- [X] Il Web supporta la modifica del disegno, il caricamento diretto delle immagini, l'algoritmo di dithering in bianco e nero
- [X] Algoritmo di dithering a tre colori, supporto display a tre colori sul lato dispositivo, supporto trasmissione Bluetooth
- [X] Dopo aver aggiornato il buffer epd, i dati sono anomali (a volte ci sono barre nere a sinistra o a destra)?
- [X] Visualizzazione cinese (parte dei caratteri cinesi viene visualizzata come bitmap, non tutti i caratteri cinesi sono supportati)
- [X] Supporta l'accesso alla rete findmy di Apple e simula airtag

### Readme.md originale

[README_EN.md](/README_en.md) (Per altri modelli, fare riferimento al progetto originale. Questo progetto supporta solo dispositivi L3N@ 2,9 pollici)

> Nota:
> Modificato in base al progetto [ATC_TLSR_Paper](https://github.com/atc1441/ATC_TLSR_Paper).

### materiale

- [Scheda tecnica TLSR8359](/docs/DS_TLSR8359-E_Scheda tecnica per Telink ULP 2.4GHz RF SoC TLSR8359.pdf)
- [Manuale di sviluppo Bluetooth tlsr8x5x (cinese)](/docs/Telink Kite BLE SDK Developer Handbook Chinese.pdf)
- [Manuale del driver dello schermo SSD1680.pdf](/docs/SSD1680.pdf)
