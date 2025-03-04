#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <time.h>

#define MAX_CLIENTS 10
#define TAILLE_BUFFER 1024
#define LOG_FILE "messages.log"

void logMessage(const char *message) {
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file) {
        time_t now = time(NULL);
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(log_file, "[%s] %s\n", time_str, message);
        fclose(log_file);
    }
}

int pseudoExiste(char pseudos[MAX_CLIENTS][50], char *nouveau_pseudo) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (pseudos[i][0] != '\0' && strcmp(pseudos[i], nouveau_pseudo) == 0) {
            return 1; // Le pseudo existe déjà
        }
    }
    return 0; // Le pseudo n'existe pas
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Utilisation: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port <= 0) {
        fprintf(stderr, "Numéro de port invalide.\n");
        return EXIT_FAILURE;
    }

    int serveur_fd, clients_sockets[MAX_CLIENTS] = {0};
    char clients_pseudos[MAX_CLIENTS][50] = {0};  // Stockage des pseudos des clients
    struct sockaddr_in adresse;
    fd_set ensemble_fds;
    char buffer[TAILLE_BUFFER];

    int option = 1;
    socklen_t taille_adresse = sizeof(adresse);

    // Création du socket serveur
    if ((serveur_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Erreur lors de la création du socket");
        return EXIT_FAILURE;
    }

    setsockopt(serveur_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    // Configuration de l'adresse du serveur
    adresse.sin_family = AF_INET;
    adresse.sin_addr.s_addr = INADDR_ANY;  // Accepter les connexions de n'importe quelle interface
    adresse.sin_port = htons(port);

    // Associer le socket au port
    if (bind(serveur_fd, (struct sockaddr *)&adresse, sizeof(adresse)) < 0) {
        perror("Échec du bind");
        close(serveur_fd);
        return EXIT_FAILURE;
    }

    // Écoute des connexions
    if (listen(serveur_fd, MAX_CLIENTS) < 0) {
        perror("Échec de l'écoute");
        close(serveur_fd);
        return EXIT_FAILURE;
    }
    printf("Serveur démarré sur %s:%d\n", inet_ntoa(adresse.sin_addr), port);
    printf("Tapez '/shutdown' pour arrêter le serveur.\n");

    while (1) {
        FD_ZERO(&ensemble_fds);
        FD_SET(serveur_fd, &ensemble_fds);
        FD_SET(STDIN_FILENO, &ensemble_fds);
        int max_sd = serveur_fd;

        // Ajout des clients existants au set
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients_sockets[i];
            if (sd > 0) FD_SET(sd, &ensemble_fds);
            if (sd > max_sd) max_sd = sd;
        }

        if (select(max_sd + 1, &ensemble_fds, NULL, NULL, NULL) < 0) {
            perror("Erreur avec select");
            continue;
        }

        // Vérifier si l'administrateur arrête le serveur
        if (FD_ISSET(STDIN_FILENO, &ensemble_fds)) {
            fgets(buffer, TAILLE_BUFFER, stdin);
            buffer[strcspn(buffer, "\n")] = 0;

            if (strcmp(buffer, "/shutdown") == 0) {
                printf("Arrêt du serveur...\n");
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients_sockets[i] != 0) close(clients_sockets[i]);
                }
                close(serveur_fd);
                return EXIT_SUCCESS;
            }
        }

        // Nouvelle connexion entrante
        if (FD_ISSET(serveur_fd, &ensemble_fds)) {
            int new_socket = accept(serveur_fd, (struct sockaddr *)&adresse, &taille_adresse);
            if (new_socket < 0) {
                perror("Erreur lors de l'acceptation");
                continue;
            }

            // Lire le pseudo du client
            int bytes_lus = read(new_socket, buffer, TAILLE_BUFFER - 1);
            if (bytes_lus <= 0) {
                close(new_socket);
                continue;
            }
            buffer[bytes_lus] = '\0';

            // Vérifier si le pseudo existe déjà
            if (pseudoExiste(clients_pseudos, buffer)) {
                char *message = "ERREUR: Ce pseudo est déjà utilisé. Veuillez vous reconnecter avec un autre pseudo.";
                send(new_socket, message, strlen(message), 0);
                close(new_socket);
                continue;
            }

            // Ajouter le client au tableau
            int client_index = -1;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients_sockets[i] == 0) {
                    clients_sockets[i] = new_socket;
                    strncpy(clients_pseudos[i], buffer, sizeof(clients_pseudos[i]) - 1);
                    clients_pseudos[i][sizeof(clients_pseudos[i]) - 1] = '\0';
                    client_index = i;
                    break;
                }
            }
            if (client_index == -1) {
                // Serveur plein, refuser la connexion
                char *message = "Serveur plein, réessayez plus tard.";
                send(new_socket, message, strlen(message), 0);
                close(new_socket);
                continue;
            }

            printf("Nouveau client connecté : %s\n", clients_pseudos[client_index]);

            // Annoncer à tous les clients qu'un nouveau utilisateur s'est connecté
            snprintf(buffer, TAILLE_BUFFER, "Serveur: %s a rejoint le chat.", clients_pseudos[client_index]);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients_sockets[i] != 0 && i != client_index) {
                    send(clients_sockets[i], buffer, strlen(buffer), 0);
                }
            }
            
            // Enregistrer dans le log
            logMessage(buffer);

            // Envoyer la liste des utilisateurs connectés au nouveau client
            char liste_utilisateurs[TAILLE_BUFFER] = "Utilisateurs connectés:\n";
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients_sockets[i] != 0) {
                    strcat(liste_utilisateurs, "- ");
                    strcat(liste_utilisateurs, clients_pseudos[i]);
                    strcat(liste_utilisateurs, "\n");
                }
            }
            send(new_socket, liste_utilisateurs, strlen(liste_utilisateurs), 0);
        }

        // Vérifier l'activité des clients connectés
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients_sockets[i];

            if (FD_ISSET(sd, &ensemble_fds)) {
                int bytes_lus = read(sd, buffer, TAILLE_BUFFER - 1);

                if (bytes_lus <= 0) {
                    // Informer les autres clients de la déconnexion
                    snprintf(buffer, TAILLE_BUFFER, "Serveur: %s a quitté le chat.", clients_pseudos[i]);
                    printf("Client déconnecté : %s\n", clients_pseudos[i]);
                    
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients_sockets[j] != 0 && j != i) {
                            send(clients_sockets[j], buffer, strlen(buffer), 0);
                        }
                    }
                    
                    // Enregistrer dans le log
                    logMessage(buffer);
                    
                    close(sd);
                    clients_sockets[i] = 0;
                    clients_pseudos[i][0] = '\0';
                } else {
                    buffer[bytes_lus] = '\0';
                    
                    // Vérifier si c'est un message de chat (format MSG:pseudo:message)
                    if (strncmp(buffer, "MSG:", 4) == 0) {
                        char pseudo[50];
                        char message_content[TAILLE_BUFFER];
                        char formatted_message[TAILLE_BUFFER];
                        
                        // Extraire le pseudo et le contenu du message
                        char *pseudo_start = buffer + 4; // Après "MSG:"
                        char *message_start = strchr(pseudo_start, ':');
                        
                        if (message_start) {
                            // Copier le pseudo
                            int pseudo_len = message_start - pseudo_start;
                            strncpy(pseudo, pseudo_start, pseudo_len);
                            pseudo[pseudo_len] = '\0';
                            
                            // Copier le contenu du message
                            strcpy(message_content, message_start + 1);
                            
                            // Formater le message pour l'affichage et le log
                            snprintf(formatted_message, TAILLE_BUFFER, "%s > %s", pseudo, message_content);
                            
                            printf("Message reçu de %s : %s\n", pseudo, message_content);
                            
                            // Enregistrer dans le log
                            logMessage(formatted_message);
                            
                            // Diffuser le message à tous les autres clients
                            for (int j = 0; j < MAX_CLIENTS; j++) {
                                if (clients_sockets[j] != 0 && clients_sockets[j] != sd) {
                                    send(clients_sockets[j], formatted_message, strlen(formatted_message), 0);
                                }
                            }
                        }
                    } else {
                        // Message de format inconnu
                        printf("Message de format inconnu reçu : %s\n", buffer);
                    }
                }
            }
        }
    }

    return EXIT_SUCCESS;
}