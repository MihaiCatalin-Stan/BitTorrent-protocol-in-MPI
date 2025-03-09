#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <string.h>
#include <stdbool.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define MAX_USERS 10

MPI_Datatype MPI_fd;

// Structura de fisier contine:
// Numele fisierului
// Numarul de segmente
// Hash-urile acestuia
typedef struct file_data {
    char name[MAX_FILENAME + 1];
    int segments;
    char data[MAX_CHUNKS + 1][HASH_SIZE + 1];
} fd;

// Un swarm contine
// Datele despre fisier
// Seederii si peerii acestuia
// Numarul lor
// Primul seeder/peer de la care sa
// ceara date
typedef struct swarm {
    fd file_data;
    int users[MAX_USERS];
    int no_of_seeders;
    int start;
} swarm;

fd owned_files[MAX_FILES];
int owned_cnt;

// Pentru thread-urile peer-urilor
// am facut o structura pentru a le transmite
// fisierul, rank-ul si variabila de finalizare
// a thread-ului de upload
struct thread_arg {
    FILE *fin;
    int rank, *done;
};

void download_file(char filename[MAX_FILENAME + 1], fd wanted_file_data, int no_of_seeders, int start, int seeders[MAX_USERS], int rank) {
    int downloaded_segments = 0;
    int current_seeder;
    char response[4] = "";
    char ack[4] = "ack";
    MPI_Status status;

    // Cat timp mai sunt de downladat segmente din fisierul curent
    while (downloaded_segments < wanted_file_data.segments) {
        current_seeder = start;
        response[0] = '\0';

        // Parcurgem seederii din lista primita de la tracker
        for (int i = 0; i < no_of_seeders; i++) {
            // Trecem la urmatorul seeder dupa fiecare segment descarcat
            current_seeder++;
            current_seeder %= no_of_seeders;

            // Nu ne trimitem cerere noua (evident)
            if (seeders[current_seeder] == rank) {
                current_seeder++;
                current_seeder %= no_of_seeders;
            }

            // Trimitem cerere cu numele fisierului dorit
            // Asteptam un ack de la peer-ul de le care cerem
            // pentru a stii ca e pregatit sa primeasca hash-ul
            // pe care il cautam.
            MPI_Send(wanted_file_data.name, MAX_FILENAME + 1, MPI_CHAR, seeders[current_seeder], 100, MPI_COMM_WORLD);
            MPI_Recv(ack, 4, MPI_CHAR, seeders[current_seeder], 102, MPI_COMM_WORLD, &status);
            MPI_Send(wanted_file_data.data[downloaded_segments], HASH_SIZE + 1, MPI_CHAR, seeders[current_seeder], 101, MPI_COMM_WORLD);
            
            // Asteptam un raspuns de tipul "ack" sau "nck"
            MPI_Recv(response, 4, MPI_CHAR, seeders[current_seeder], 102, MPI_COMM_WORLD, &status);
            
            if (!strcmp(response, "ack")) {
                downloaded_segments++;
                break;
            }
        }

        // La fiecare 10 segmentele downloadate cerem datele
        // si listele updatate de la tracker
        if (downloaded_segments % 10 == 0) {
            MPI_Send(filename, MAX_FILENAME + 1, MPI_CHAR, 0, 2, MPI_COMM_WORLD);

            fd wanted_file_data;
            int no_of_seeders, start;
            MPI_Recv(&wanted_file_data, 1, MPI_fd, 0, 2, MPI_COMM_WORLD, &status);
            MPI_Send(ack, 4, MPI_CHAR, 0, 2, MPI_COMM_WORLD);

            MPI_Recv(&no_of_seeders, 1, MPI_INT, 0, 2, MPI_COMM_WORLD, &status);
            MPI_Send(ack, 4, MPI_CHAR, 0, 2, MPI_COMM_WORLD);

            MPI_Recv(&start, 1, MPI_INT, 0, 2, MPI_COMM_WORLD, &status);
            MPI_Send(ack, 4, MPI_CHAR, 0, 2, MPI_COMM_WORLD);

            int seeders[MAX_USERS];
            MPI_Recv(seeders, no_of_seeders, MPI_INT, 0, 2, MPI_COMM_WORLD, &status);
        }
    }
}

void *download_thread_func(void *arg)
{
    struct thread_arg t_arg = *(struct thread_arg*) arg;
    FILE *fin = t_arg.fin;
    int rank = t_arg.rank;
    int wanted_cnt;

    MPI_Status status;
    char ack[4] = "ack";
    char outfile_name[MAX_FILENAME + 1] = "client";
    outfile_name[6] = rank + '0';
    outfile_name[7] = '\0';

    fscanf(fin, "%d", &wanted_cnt);
    for (int i = 0; i < wanted_cnt; i++) {
        char filename[MAX_FILENAME + 1];

        fscanf(fin, "%s", filename);

        // Trimitem o cerere catre tracker pentru fisierul
        // cu numele citit din fisierul de intrare
        MPI_Send(filename, MAX_FILENAME + 1, MPI_CHAR, 0, 2, MPI_COMM_WORLD);

        // Asteptam datele cerute de la tracker
        fd wanted_file_data;
        int no_of_seeders, start;
        MPI_Recv(&wanted_file_data, 1, MPI_fd, 0, 2, MPI_COMM_WORLD, &status);
        MPI_Send(ack, 4, MPI_CHAR, 0, 2, MPI_COMM_WORLD);

        MPI_Recv(&no_of_seeders, 1, MPI_INT, 0, 2, MPI_COMM_WORLD, &status);
        MPI_Send(ack, 4, MPI_CHAR, 0, 2, MPI_COMM_WORLD);

        MPI_Recv(&start, 1, MPI_INT, 0, 2, MPI_COMM_WORLD, &status);
        MPI_Send(ack, 4, MPI_CHAR, 0, 2, MPI_COMM_WORLD);

        int seeders[MAX_USERS];
        MPI_Recv(seeders, no_of_seeders, MPI_INT, 0, 2, MPI_COMM_WORLD, &status);

        // Downloadam fisierul de la seederi
        download_file(filename, wanted_file_data, no_of_seeders, start, seeders, rank);

        // Am terminat de DL fisierul curent
        MPI_Send(filename, MAX_FILENAME + 1, MPI_CHAR, 0, 3, MPI_COMM_WORLD);

        // Formam numele fisierului de output
        outfile_name[7] = '\0';
        strcat(outfile_name, "_");
        strcat(outfile_name, wanted_file_data.name);

        // Printam in fisierul de output informatiile despre fisierul downloadat
        FILE *fout = fopen(outfile_name, "w");
        for (int j = 0; j < wanted_file_data.segments - 1; j++) {
            fprintf(fout, "%s\n", wanted_file_data.data[j]);
        }
        fprintf(fout, "%s", wanted_file_data.data[wanted_file_data.segments - 1]);

        fclose(fout);
    }

    // Am terminat de DL toate fisierele
    MPI_Send(ack, 4, MPI_CHAR, 0, 4, MPI_COMM_WORLD);

    return NULL;
}

void *upload_thread_func(void *arg)
{
    struct thread_arg t_arg = *(struct thread_arg*) arg;
    int rank = t_arg.rank;
    MPI_Status status;
    char ack[4] = "ack";
    // Stiu ca opusul ar fi nack, dar nu voiam sa fac siruri de size-uri diferite :)
    char nck[4] = "nck";

    // Thread-ul de upload lucreaza pana cand variabila de done e updatata
    // de main thread
    while (!(*t_arg.done)) {
        char filename[MAX_FILENAME + 1], hash[HASH_SIZE + 1];

        // Asteptam o cerere de fisier
        MPI_Recv(filename, MAX_FILENAME + 1, MPI_CHAR, MPI_ANY_SOURCE, 100, MPI_COMM_WORLD, &status);

        // Daca cererea e de la tracker, atunci inseamna ca toti au terminat
        // si putem inchide bucla
        if (status.MPI_SOURCE == 0) {
            break;
        }

        // Trimitem o confirmare a primirii cererii si asteptam hash-ul dorit
        MPI_Send(ack, 4, MPI_CHAR, status.MPI_SOURCE, 102, MPI_COMM_WORLD);
        MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, 101, MPI_COMM_WORLD, &status);

        // Verificam prezenta fisierului si a hash-ului
        bool own = false;

        for (int i = 0; i < owned_cnt; i++) {
            if (!strcmp(owned_files[i].name, filename)) {
                fd requested_file = owned_files[i];

                for (int j = 0; j < requested_file.segments; j++) {
                    if (!strcmp(requested_file.data[j], hash)) {
                        own = true;
                        break;
                    }
                }
                break;
            }
        }

        // Daca il avem, trimitem ack, altfel trimitem nck (nack)
        if (own) {
            MPI_Send(ack, 4, MPI_CHAR, status.MPI_SOURCE, 102, MPI_COMM_WORLD);
        } else {
            MPI_Send(nck, 4, MPI_CHAR, status.MPI_SOURCE, 102, MPI_COMM_WORLD);
        }
    }

    return NULL;
}

void tracker(int numtasks, int rank) {
    std::map<std::string, swarm> tracker_data;
    int no_of_files = 0;

    MPI_Status status;

    char ack[4] = "ACK";

    // Asteptam sa primim date de la toti peers
    for (int i = 0; i < numtasks - 1; i++) {
        fd files[MAX_FILES];
        int recv_count;

        // 1 - Doresc sa comunic fisierele pentru care sunt seeder, si te anunt cate am
        // 0 - Comunic fisierele pentru care sunt seeder
        MPI_Recv(&recv_count, 1, MPI_INT, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        MPI_Send(ack, 4, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
        MPI_Recv(files, recv_count, MPI_fd, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

        for (int j = 0; j < recv_count; j++) {
            // Adauga in map la cheia files[j].name, valoarea status.source;
            std::string key = files[j].name;

            auto it = tracker_data.find(key);
            // Deja exista date despre acest fisier in map
            // Updatam seederii in referinta intrarii in map
            if (it != tracker_data.end()) {
                swarm &file_swarm = tracker_data[key];
                file_swarm.users[file_swarm.no_of_seeders] = status.MPI_SOURCE;
                file_swarm.no_of_seeders++;

            // Fisierul este nou, copiem datele intr-un swarm, completam
            // celelalte campuri si il adaugam in map
            } else {
                swarm file_swarm;
                memcpy(&file_swarm.file_data, &files[j], sizeof(fd));
                file_swarm.users[0] = status.MPI_SOURCE;
                file_swarm.no_of_seeders = 1;
                file_swarm.start = 0;

                tracker_data.insert({key, file_swarm});
            }
        }
    }

    // Dupa ce toti peers au trimis datele, putem continua
    MPI_Barrier(MPI_COMM_WORLD);

    int downloads_done = 0;
    char name[MAX_FILENAME + 1];

    while (downloads_done < numtasks - 1) {
        // 2 - Cer fisierul X - Trimit swarm ul fisierului X
        // 3 - Finalizare descarcare Y - Luam sursa din status
        // 4 - Finalizare toate descarcarile - Luam sursa din status
        MPI_Recv(name, MAX_FILENAME + 1, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        int tag = status.MPI_TAG;

        switch (tag) {
            case 2: {
                std::string key = name;
                swarm &wanted_file_swarm = tracker_data[key];

                // Trimitem datele despre fisierul cautat
                MPI_Send(&(wanted_file_swarm.file_data), 1, MPI_fd, status.MPI_SOURCE, 2, MPI_COMM_WORLD);
                MPI_Recv(ack, 4, MPI_CHAR, status.MPI_SOURCE, 2, MPI_COMM_WORLD, &status);

                // Trimitem numarul de seederi
                MPI_Send(&(wanted_file_swarm.no_of_seeders), 1, MPI_INT, status.MPI_SOURCE, 2, MPI_COMM_WORLD);
                MPI_Recv(ack, 4, MPI_CHAR, status.MPI_SOURCE, 2, MPI_COMM_WORLD, &status);

                // Trimitem primul seeder de la care sa inceapa
                // pentru a evita congestia unui singur user
                MPI_Send(&(wanted_file_swarm.start), 1, MPI_INT, status.MPI_SOURCE, 2, MPI_COMM_WORLD);
                MPI_Recv(ack, 4, MPI_CHAR, status.MPI_SOURCE, 2, MPI_COMM_WORLD, &status);

                // Trimitem si seederii fisierului cautat
                MPI_Send(wanted_file_swarm.users, wanted_file_swarm.no_of_seeders, MPI_INT, status.MPI_SOURCE, 2, MPI_COMM_WORLD);
                
                // Mutam punctul de start al parcurgerii seederilor
                // pentru fisierul curent pentru a evita congestia
                wanted_file_swarm.start++;
                wanted_file_swarm.start %= wanted_file_swarm.no_of_seeders;

                // Adaugam user-ul care a cerut fisierul ca peer pentru acesta
                // Astfel va putea transmite mai departe segmentele pe care
                // deja le-a descarcat
                // Adaugarea se face doar in cazul in care nu exista deja
                bool exists = false;
                for (int i = 0; i < wanted_file_swarm.no_of_seeders; i++) {
                    if (wanted_file_swarm.users[i] == status.MPI_SOURCE) {
                        exists = true;
                        break;
                    }
                }

                if (!exists) {
                    wanted_file_swarm.users[wanted_file_swarm.no_of_seeders] = status.MPI_SOURCE;
                    wanted_file_swarm.no_of_seeders++;
                }
                
                break;
            }
            case 4: {
                // Daca toate download-urile s-au terminat, incrementam
                // counter-ul de peers care si-au terminat download-urile
                // si coboram si in cazul 3 unde il marcam ca seeder
                downloads_done++;
            }
            case 3: {
                std::string key = name;
                swarm &wanted_file_swarm = tracker_data[key];

                // Il marcam pe user ca seeder (ca in cazul 2 din moment)
                // ce lista de peers si seeders e aceeasi
                bool exists = false;
                for (int i = 0; i < wanted_file_swarm.no_of_seeders; i++) {
                    if (wanted_file_swarm.users[i] == status.MPI_SOURCE) {
                        exists = true;
                        break;
                    }
                }

                if (!exists) {
                    wanted_file_swarm.users[wanted_file_swarm.no_of_seeders] = status.MPI_SOURCE;
                    wanted_file_swarm.no_of_seeders++;
                }
                break;
            }
            default: {
                printf("Opa, nu aici bossule!\n");
                break;
            }
        }
    }

    // Toti userii au terminat deci anuntam thread-ul principal din fiecare
    // peer sa isi inchida thread-ul de upload
    MPI_Bcast(ack, 4, MPI_CHAR, 0, MPI_COMM_WORLD);

    // Thread-urile de upload blocate intr-un Recv se deblocheaza pe rand
    for (int i = 1; i < numtasks; i++) {
        MPI_Send("", MAX_FILENAME + 1, MPI_CHAR, i, 100, MPI_COMM_WORLD);
    }

    return;
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    MPI_Status mstatus;
    int r;
    char ack[4] = "\0";

    // Compunem numele fisierului de intrare
    char fin_name[MAX_FILENAME + 1] = "";
    strcat(fin_name, "in");
    fin_name[2] = rank + '0';
    fin_name[3] = '\0';
    strcat(fin_name, ".txt");
    fin_name[strlen(fin_name)] = '\0';

    // Deschidem fisierul si citim datele fisierelor pe care le avem
    FILE *fin = fopen(fin_name, "r");
    if (!fin) {
        fprintf(stderr, "File opening failed from rank %d\n", rank);
    }

    fscanf(fin, "%d", &owned_cnt);

    for (int i = 0; i < owned_cnt; i++) {
        fscanf(fin, "%s %d", owned_files[i].name, &owned_files[i].segments);

        for (int j = 0; j < owned_files[i].segments; j++) {
            fscanf(fin, "%s", owned_files[i].data[j]);
        }
    }

    // Initiem o conexiune cu tracker-ul in care ii spunem intai cate
    // fisierele are de primit
    // Dupa ce primim un ack (tracker-ul accepta conexiunea), ii trimitem
    // si vectorul cu datele despre fisierele detinute
    MPI_Send(&owned_cnt, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);
    MPI_Recv(ack, 4, MPI_CHAR, 0, 1, MPI_COMM_WORLD, &mstatus);
    MPI_Send(owned_files, owned_cnt, MPI_fd, 0, 0, MPI_COMM_WORLD);

    // Nu pornim partea de download/upload pana ce nu au trimis toti peers datele cu care pornesc
    MPI_Barrier(MPI_COMM_WORLD);

    struct thread_arg arg;
    arg.fin = fin;
    arg.rank = rank;
    int done = 0;
    arg.done = &done;
    
    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &arg);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &arg);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    // Asteptam ca tracker-ul sa ne anunte ca toti au terminat de downloadat
    // fisierele pe care le voiau
    MPI_Bcast(ack, 4, MPI_CHAR, 0, MPI_COMM_WORLD);
    done = 1;

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Declaram structura MPI pentru struct fd
    MPI_Datatype oldtypes[3];
    int blockcounts[3];
    MPI_Aint offsets[3];
    offsets[0] = offsetof(fd, name);
    oldtypes[0] = MPI_CHAR;
    blockcounts[0] = MAX_FILENAME + 1;

    offsets[1] = offsetof(fd, segments);
    oldtypes[1] = MPI_INT;
    blockcounts[1] = 1;

    offsets[2] = offsetof(fd, data);
    oldtypes[2] = MPI_CHAR;
    blockcounts[2] = (MAX_CHUNKS + 1) * (HASH_SIZE + 1);

    MPI_Type_create_struct(3, blockcounts, offsets, oldtypes, &MPI_fd);
    MPI_Type_commit(&MPI_fd);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
