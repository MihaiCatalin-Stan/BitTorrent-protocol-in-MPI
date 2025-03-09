Stan Mihai-Catalin 335CA
# Implementare protocol BitTorrent in MPI

## Structuri folosite
```cpp
typedef struct file_data {
    char name[MAX_FILENAME + 1];
    int segments;
    char data[MAX_CHUNKS + 1][HASH_SIZE + 1];
} fd;
```
Structura de fisier definita si ca structura in MPI sub numele `MPI_fd`. Contine numele fisierului, numarul de segmente/hash-uri ale acestuia si hash-urile.

```cpp
typedef struct swarm {
    fd file_data;
    int users[MAX_USERS];
    int no_of_seeders;
    int start;
} swarm;
```
Structura de swarm este folosita doar de catre tracker si contine datele despre un fisier, alaturi de numarul de seederi + peeri, care sunt acestia si indicele din lista de la care sa inceapa urmatorul peer sa ceara date. Astfel, evitam congestia unui singur user si ne asiguram ca workload-ul pentru download-ul unui fisier este distribuit egal.

```cpp
struct thread_arg {
    FILE *fin;
    int rank, *done;
};
```
Structura data ca argument thread-urilor peeriilor. `fin` este fisierul din care thread-ul de download citeste numele fisierelor dorite. Rank este rank-ul procesului curent, iar `done` este o variabila modificata de thread-ul principal, in urma primirii unui semnal de la tracker pentru incheierea transferurilor. Acesta este folosit doar de thread-ul de upload.

## Rolurile proceselor
`Tracker`:
* Are rank-ul 0 in ierarhia de procese
* Tine evidenta fisierelor existente si a userilor care le detin
* Tine evidenta userilor care au terminat de descarcat fisierele
* Trimite date despre fisierele dorite de un user

`Peers`:
* Au orice alt rank in afara de 0
* Citesc fisierele detinute si le trimit la tracker
* Se ocupa de `download` si `upload`

`Download`:
* Citeste fisierele dorite
* Ia legatura cu tracker-ul si cere informatiile despre seederi
* Ia legatura cu seederii in legatura cu hash-ul dorit

`Upload`:
* Asteapta cerere de fisier
* Verifica daca detine hash-ul cerut
* Trimite un mesaj de confirm/reject

## Executia programului
Initializare:
* `Tracker-ul`:
    * Asteapta sa primeasca date despre fisierele detinute de la fiecare user (chiar daca nu are nimic).
    * Citeste datele primite si le adauga in map. Daca deja exista in map, updateaza lista de seederi din swarm-ul acelui fisier
* `Peerii`:
    * Citesc datele despre fisierele detinute.
    * Trimit aceste date la tracker.
    * Se imparte in doua thread-uri, download_thread si upload_thread
* Se asteapta finalizarea etapei de initializare cu MPI_Barrier

Transferul de fisiere:
* `Tracker-ul`:
    * Asteapta primirea unui nou mesaj. Tipul mesajului este diferentiat prin tag astfel:
        * 2 - Cerere swarm fisier
        * 3 - Finalizare descarcare fisier
        * 4 - Finalizare toate descarcarile
    * Pentru **cerere swarm fisier**:
        * Trimitem datele despre fisierul cautat (structura de fd)
        * Trimitem celelalte campuri din swarm pe rand, cu asteptare unei confirmari intre ele
        * Schimbam seeder-ul de la care se va incepe urmatoarea descarcare a fisierului cautat, pentru a evita congestia acestuia in cazul in care mai multi doresc sa descarce acelasi fisier
        * Adaugam user-ul nou in lista de seederi pentru a putea partaja si el din segmentele pe care a inceput sa le descarce.
    * Pentru **finalizare descarcare fisier**:
        * Doar adaugam user-ul in lista de seederi, avand grija sa nu existe deja
    * Pentru **finalizare descarcare toate fisierele**:
        * Incrementam variabila de contorizare a user-ilor care au terminat de descarcat
        * Facem acelasi lucru ca la punctul anterior
    * La final, anunta peerii (printr-un MPI_Bcast) ca s-a incheiat executia programului si pot elibera thread-urile de upload
    * Elibereaza si el thread-urile blocate intr-un apel de receive blocant
* `download_thread`:
    * Citeste numele fisierelor dorite
    * Pentru fiecare fisier:
        * Cere datele de la tracker
        * Cicleaza prin seederi cerand pe rand cate un segment de la fiecare
        * La fiecare 10 segmente downloadate cere lista updatata de la tracker pentru a imparti workload-ul la mai multi useri
        * La final, scrie datele descoperite si confirmate despre fisierul respectiv
        * La final, anunta tracker-ul ca a terminat de descarcat fisierul X
    * Anunta tracker-ul ca a terminat de descarcat toate fisierel
* `upload_thread`:
    * Cicleaza in asteptarea unor cereri de download a fisierelor detinute de acesta
    * Daca primeste mesaj de la tracker, inseamna ca s-a incheiat executia programului si poate iesi din structura repetitiva
    * Daca primeste mesaj de la un peer, initializeaza conexiunea cu download_thread-ul respectiv trimitandu-i un ack
    * Mai departe asteapta primirea unui hash de la acelasi peer
    * Cauta existenta acelui fisier si hash in ceea ce detine
    * Trimite un accept(ack) sau reject(nck) ca raspuns la cerere

## Observatii
Pentru trimiterea mai multor date intre aceleasi doua procese fara a permite comunicarii sa fie intrerupta de un alt proces, am avut trei abordari:
1. Am creat structura de MPI_fd pentru a trimite toate datele despre un fisier simultan.
2. Am folosit tag-uri diferite in functie de mesajul trimis. (ex. 100 - cerere fisier, 101 - cerere hash, 102 - ack in transferul de fisiere)
3. In parte sender-ului am asteptat primirii unui ACK pentru a ma asigura ca toate mesajele au fost primite in ordine. (ex. liniile 145-155)
