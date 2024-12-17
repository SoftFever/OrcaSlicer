# Pellet modded printer
Le stampanti 3D a pellet, comunemente utilizzate per stampe di grandi dimensioni (solitamente superiori a 1m³), si basano su una tecnologia simile alla stampa FDM. Tuttavia, anziché utilizzare filamenti, queste stampanti impiegano pellet come materiale di stampa.
L'uso dei pellet comporta una gestione dell'estrusione diversa rispetto alle stampanti a filamento tradizionali, a causa di:

- La presenza di aria tra i pellet.
- Dimensioni variabili del particolato a seconda del materiale.
- Necessità di sistemi di alimentazione attivi.

Questo richiede parametri aggiuntivi per una corretta configurazione dell'estrusione.

## pellet flow coefficient
Il pellet flow coefficient è un parametro che rappresenta la capacità di estrusione dei pellet, influenzata da fattori quali:

- Forma del pellet.
- Tipo di materiale.
- Viscosità del materiale.

### Funzionamento
Questo valore definisce la quantità di materiale estruso per ogni giro del meccanismo di alimentazione. Internamente, il coefficiente viene convertito in un valore equivalente di `filament_diameter` per garantire compatibilità con i calcoli volumetrici standard FDM.

La formula utilizzata è: *filament_diameter = sqrt( (4 \* pellet_flow_coefficient) / PI )*

- Maggiore densità di riempimento → Maggior materiale estruso → Coefficiente di flusso più alto → Simulazione di un filamento con diametro maggiore.
- Una riduzione del coefficiente del 20% corrisponde a una riduzione lineare del flusso del 20%.

In stampanti dove non è possibile regolare la distanza di rotazione del motore dell'estrusore, il pellet_flow_coefficient permette di controllare la percentuale di flusso modificando il diametro del filamento simulato.

>**ATTENZIONE:** Modulare l'estrusione modificando il diametro del filamento virtuale causa tuttavia un errore di lettura della quantità estrusa dalla stampante all'interno del firmware utilizzato.
Ad esempio su un firmware klipper si avrà una lettura a schermo del flusso di stampa differente da quello realmente in corso.
utilizzare questo metodo causa anche problemi con il valore di retraction e unretraction, in quanto la conversione col filamneto virtuale altera i valori reali. 



## Extruder Rotation Volume
La extruder rotation volume rappresenta il volume di materiale estruso (in mm³) per ogni giro completo del motore dell'estrusore. Questo parametro offre una maggiore precisione nella configurazione rispetto al pellet flow coefficient, eliminando errori di calcolo dovuti alla simulazione del diametro del filamento virtuale.

### Configurazione tramite Filamento Virtuale con Area di 1 mm²
Per semplificare ulteriormente il calcolo del volume estruso e sincronizzare i parametri della stampante con i valori di volume nel G-code, è possibile simulare un filamento virtuale con un'area trasversale di 1 mm². In questo modo, i valori di estrusione 𝐸 nel G-code rappresentano direttamente il volume in mm³.

#### Passaggi per l'implementazione
1. **Impostazione del Pellet Flow Coefficient:** Simulare un filamento con un'area trasversale di 1 mm² impostando il pellet flow coefficent ad 1.
2. **Configurazione della Extruder Rotation Volume:** Determina quanti mm³ di materiale vengono estrusi per ogni giro completo del motore dell'estrusore. Questo parametro dipende dalla geometria della vite o dell'ingranaggio dell'estrusore e può essere calibrato sperimentalmente.
Inserisci il valore ottenuto nella configurazione del materiale come extruder rotation volume.
3. **Imposta il filament_diameter** pari a 1.1284 all'interno del firmware della proprio stampante

### Vantaggi del Sistema
- Eliminazione degli errori di conversione: Non è necessario simulare filamenti di diverso diametro, riducendo gli errori.
- Maggiore precisione: L'utilizzo del volume estruso diretto nel G-code rende la calibrazione più semplice e accurata.
- Compatibilità dinamica: Con firmware come Klipper, puoi aggiornare i parametri in tempo reale senza dover riscrivere il G-code.

>**ATTENZIONE:** Questa funzionalità è attualmente supportata solo da stampanti con firmware Klipper. Assicurati di verificare la compatibilità prima di procedere con la configurazione.



## Mixing Stepper Rotation Volume
La mixing stepper rotation volume indica il volume di materiale (in mm³) immesso attivamente nell'estrusore tramite un motore dedicato.

### Utilizzo
Per utilizzare questa funzionalità, è necessario:

- Abilitare l'opzione nelle impostazioni della stampante.
- Definire il nome identificativo del motore che gestisce il feeding nelle impostazioni dell'estrusore.
- Configurare il valore di mixing_stepper_rotation_volume nelle impostazioni del materiale.

>**ATTENZIONE:** Questa funzionalità è attualmente supportata solo da stampanti con firmware Klipper. Assicurati di verificare la compatibilità prima di procedere con la configurazione.