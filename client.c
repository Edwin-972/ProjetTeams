#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define TAILLE_BUFFER 1024
#define LOG_FILE "messages.log" // Nom du fichier log

void afficher_logs() {
    FILE *log_file = fopen(LOG_FILE, "r");
    if (log_file) {
        printf("\n=== Historique des messages ===\n");
        char line[TAILLE_BUFFER];
        while (fgets(line, TAILLE_BUFFER, log_file)) {
            printf("%s", line);
        }
        printf("===============================\n\n");
        fclose(log_file);
    } else {
        printf("\nAucun historique de messages disponible.\n\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Utilisation: %s <IP_serveur> <Port> <Pseudo>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *ip_serveur = argv[1];
    int port = atoi(argv[2]);
    char *pseudo = argv[3];

    if (port <= 0) {
        fprintf(stderr, "Numéro de port invalide.\n");
        return EXIT_FAILURE;
    }

    int socket_client;
    struct sockaddr_in adresse_serveur;
    char buffer[TAILLE_BUFFER];

    // Création du socket
    if ((socket_client = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Erreur lors de la création du socket");
        return EXIT_FAILURE;
    }

    // Configuration du serveur
    adresse_serveur.sin_family = AF_INET;
    adresse_serveur.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_serveur, &adresse_serveur.sin_addr) <= 0) {
        perror("Adresse IP invalide");
        close(socket_client);
        return EXIT_FAILURE;
    }

    // Connexion au serveur
    if (connect(socket_client, (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0) {
        perror("Échec de la connexion");
        close(socket_client);
        return EXIT_FAILURE;
    }

    printf("Connexion au serveur %s:%d... OK.\n", ip_serveur, port);
    printf("Bienvenu %s\n", pseudo);

    // Envoyer le pseudo au serveur après la connexion
    send(socket_client, pseudo, strlen(pseudo), 0);

    // Lire la liste des utilisateurs connectés envoyée par le serveur
    int bytes_lus = read(socket_client, buffer, TAILLE_BUFFER - 1);
    if (bytes_lus > 0) {
        buffer[bytes_lus] = '\0';
        
        // Vérifier si le message est une erreur de pseudo
        if (strncmp(buffer, "ERREUR:", 7) == 0) {
            printf("%s\n", buffer);
            close(socket_client);
            return EXIT_FAILURE;
        }
        
        printf("%s\n", buffer); // Afficher la liste des utilisateurs connectés
    }
    
    afficher_logs();

    printf("Tapez '/quit' pour quitter.\n");
    printf("Envoyez un nouveau message : ");
    fflush(stdout);

    fd_set ensemble_fds;

    while (1) {
        FD_ZERO(&ensemble_fds);
        FD_SET(STDIN_FILENO, &ensemble_fds); // Entrée utilisateur
        FD_SET(socket_client, &ensemble_fds); // Messages du serveur

        if (select(socket_client + 1, &ensemble_fds, NULL, NULL, NULL) < 0) {
            perror("Erreur avec select");
            break;
        }

        // Lecture de l'entrée utilisateur
        if (FD_ISSET(STDIN_FILENO, &ensemble_fds)) {
            if (fgets(buffer, TAILLE_BUFFER, stdin) == NULL)
                break;
            buffer[strcspn(buffer, "\n")] = 0;

            if (strcmp(buffer, "/quit") == 0) {
                printf("Déconnexion...\n");
                break;
            }

            // Envoyer le message au format normal (sans 'moi')
            char message_to_send[TAILLE_BUFFER];
            snprintf(message_to_send, TAILLE_BUFFER, "MSG:%s:%s", pseudo, buffer);
            send(socket_client, message_to_send, strlen(message_to_send), 0);

            // Afficher localement avec (moi)
            char local_display[TAILLE_BUFFER];
            snprintf(local_display, TAILLE_BUFFER, "%s(moi) > %s", pseudo, buffer);
            
            // Effacer la ligne précédente et afficher proprement
            printf("\r\033[K%s\n", local_display);
            printf("Envoyez un nouveau message : ");
            fflush(stdout);
        }
        
        // Lecture des messages du serveur
        if (FD_ISSET(socket_client, &ensemble_fds)) {
            int bytes_lus = read(socket_client, buffer, TAILLE_BUFFER - 1);
            if (bytes_lus <= 0) {
                printf("\nLe serveur s'est déconnecté.\n");
                break;
            }
            buffer[bytes_lus] = '\0';

            // Effacer la ligne précédente, afficher le message reçu, puis l'invite proprement
            printf("\r\033[K%s\n", buffer);
            printf("Envoyez un nouveau message : ");
            fflush(stdout);
        }
    }

    close(socket_client);
    return EXIT_SUCCESS;
}