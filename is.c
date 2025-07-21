/* See LICENSE file for license details */
/* is - minimal irc client */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <time.h>
#include <termios.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "config.h"

#define version "0.1"

int th = term_h;
int tw = term_w;

SSL_CTX *c = NULL;
SSL *s = NULL;

int using_tls = 0;

char input_buf[max_input] = {0};
int input_pos = 0;

char *messages[max_messages];
int message_cn = 0;

void get_term_sz(){
	struct winsize s;
	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &s) == 0){
		th = s.ws_row;
		tw = s.ws_col;
	}
}

void enable_raw_m(){
	struct termios t;
	tcgetattr(STDIN_FILENO, &t);
	t.c_lflag &= ~(ICANON|ECHO);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
}

void disable_raw_m(){
	struct termios t;
	tcgetattr(STDIN_FILENO, &t);
	t.c_lflag |= (ICANON|ECHO);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
}

void clearscrn(){
	printf(esc "[2J" esc "[H");
	fflush(stdout);
}

void message(const char *m){
	if(message_cn == max_messages){
		free(messages[0]);
		for(int i = 1; i < message_cn; i++) messages[i - 1] = messages[i];
		message_cn--;
	}

	messages[message_cn++] = strdup(m);
}

void list_messages(){
	int max_lines = th - 2;
	if(max_lines > max_messages) max_lines = max_messages;
	for(int i = 0; i < max_lines; i++){
		printf(esc "[%d;1H", i + 1);
		printf(esc "[2K");
	}

	int start = message_cn > max_lines ? (message_cn - max_lines) : 0;
	int line = 1;
	for(int i = start; i < message_cn; i++, line++){
		printf(esc "[%d;1H", line);
		printf("%s", messages[i]);
	}

	fflush(stdout);
}

void list_status_line(const char *nick, const char *server, const char *channel){
	printf(esc "[%d;1H", th- 1);
	printf(esc "[2K");
	printf("[%s] [%s] [%s]", nick, server, channel);
}

void list_output(const char *input_buf){
	printf(esc "[%d;1H", th);
	printf(esc "[2K");
	printf("-> %s", input_buf);
	fflush(stdout);
}

void print_server_line(const char *line){
	static char nbuf[8192] = "";
	char buf[bufsz];
	char ts[16];
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	strftime(ts, sizeof(ts), timestamp, tm);
	char prefix[256] = "", command[16] = "", target[128] = "", m[512] = "";
	if(line[0] == ':'){
		sscanf(line, ":%255s %15s %127s :%511[^\r\n]", prefix, command, target, m);
	} else {
		sscanf(line, "%15s %127s :%511[^\r\n]", command, target, m);
	}

	char nick[64] = "";
	if(prefix[0]){
		char *e = strchr(prefix, '!');
		if(e){
			size_t ln = e - prefix;
			if(ln >= sizeof(nick)) ln = sizeof(nick) - 1;
			strncpy(nick, prefix, ln);
			nick[ln] = '\0';
		} else {
			strncpy(nick, prefix, sizeof(nick)-1);
			nick[sizeof(nick)-1] = '\0';
		}
	}



	int c = atoi(command);

	if(c >= 300 && c < 400){
		if(c == 372 || c == 375 || c == 372 || c == 376 || c == 422){
			char *p = strchr(m, ':');
			if(p) p++; else p = m;
			snprintf(buf, sizeof(buf), "%s [+] motd: %s\n", ts, p);
			message(buf);
			list_messages();
		} else if(c == 332){
			snprintf(buf, sizeof(buf), "%s [+] topic: %s\n", ts, m);
			message(buf);
			list_messages();
		} else if(c == 333){
			snprintf(buf, sizeof(buf), "%s [+] topic set by: %s\n", ts, m);
			message(buf);
			list_messages();
		} else if (c == 353){
			char prefix2[256] = "", command2[16] = "", nick_in_353[64] = "", symbol[8] = "", channel[128] = "", nicks[512] = "";
			sscanf(line, ":%255s %15s %63s %7s %127s :%511[^\r\n]", prefix2, command2, nick_in_353, symbol, channel, nicks);
			if(strlen(nbuf) + strlen(nicks) + 2 < sizeof(nbuf)){
				if(nbuf[0] != '\0') strcat(nbuf, " ");
				strcat(nbuf, nicks);
			}
		} else if (c == 366){
			int len = strlen(nbuf);
			int pos = 0;
			while(pos < len){
				char line[80] = {0};
				int chunk_len = (len - pos) > 70 ? 70 : (len - pos);
				strncpy(line, nbuf + pos, chunk_len);
				line[chunk_len] = '\0';
				snprintf(buf, sizeof(buf), "%s [+] nicks: %s\n", ts, line);
				message(buf);
				list_messages();
				pos += chunk_len;
			}

			nbuf[0] = '\0';
		} else {
			snprintf(buf, sizeof(buf), "%s [+] reply %d: %s\n", ts, c, m);
			message(buf);
			list_messages();
		}				
	} else if(c >= 400 && c < 600){
		snprintf(buf, sizeof(buf), "%s [+] notice: %s\n", ts, m);
		message(buf);
		list_messages();
	} else if(strcmp(command, "NOTICE") == 0){
		snprintf(buf, sizeof(buf), "%s [+] notice: %s\n", ts, m);
		message(buf);
		list_messages();
	} else if(strcmp(command, "PRIVMSG") == 0){
		snprintf(buf, sizeof(buf), "%s [%s] %s\n", ts, nick, m);
		message(buf);
		list_messages();
	} else if(strcmp(command, "JOIN") == 0){
		snprintf(buf, sizeof(buf), "%s [+] %s has joined the channel\n", ts, nick);
		message(buf);
		list_messages();
	} else if(strcmp(command, "PART") == 0){
		snprintf(buf, sizeof(buf), "%s [+] %s has left the channel\n", ts, nick);
		message(buf);
		list_messages();
	} else if(strcmp(command, "QUIT") == 0){
		snprintf(buf, sizeof(buf), "%s [+] %s has quit\n", ts, nick);
		message(buf);
		list_messages();
	} else if(strcmp(command, "NICK") == 0){
		const char *newnick = (*m) ? m : target;
		if(newnick[0] == ':') newnick++;
		snprintf(buf, sizeof(buf), "%s [+] %s changed nick to %s\n", ts, nick, newnick);
		message(buf);
		list_messages();
	} else if(strcmp(command, "MODE") == 0){
		snprintf(buf, sizeof(buf), "%s [+] %s set mode %s\n", ts, nick, m);
		message(buf);
		list_messages();
	} else {
		char *p = strchr(m, ':');
		if(p) p++; else p = m;
		snprintf(buf, sizeof(buf), "%s [+] %s\n", ts, p);
		message(buf);
		list_messages();
	}
}

void print_user_line(const char *nick, const char *m){
	char buf[bufsz];
	char ts[16];
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	strftime(ts, sizeof(ts), timestamp, tm);
	snprintf(buf, sizeof(buf), usermf, ts, nick, m);
	message(buf);
	list_messages();
}

void s_send(int sockfd, const char *m){
	if(using_tls){
		SSL_write(s, m, strlen(m));
	} else {
		send(sockfd, m, strlen(m), 0);
	}
}

void s_raw(int sockfd, const char *m, ...){
	char buf[bufsz];
	va_list a;
	va_start(a, m);
	vsnprintf(buf, sizeof(buf), m, a);
	va_end(a);
	s_send(sockfd, buf);
}

int s_recv(int sockfd, char *buf, int len){
	if(using_tls){
		int r = SSL_read(s, buf, len);
		if(r <= 0){
			int e = SSL_get_error(s, r);
			if(e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE){
				return 0;
			} else {
				fprintf(stderr, "%d, %s\n", e, ERR_error_string(ERR_get_error(), NULL));
				return -1;
			}
		}

		return r;
	} else {
		int r = recv(sockfd, buf, len, 0);
		if(r <= 0){
			return -1;
		}

		return r;
	}
}

int connect_to_server(const char *host, int port){
	struct hostent *h = gethostbyname(host);
	if(!h) return -1;
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0) return -1;
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memcpy(&addr.sin_addr, h->h_addr, h->h_length);
	if(connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
		close(sockfd);
		return -1;
	}

	return sockfd;
}

int connect_to_server_tls(const char *host, int port){
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	c = SSL_CTX_new(TLS_client_method());
	if(!c) return -1;
	int sockfd = connect_to_server(host, port);
	if(sockfd < 0) return -1;
	s = SSL_new(c);
	if(!s) return -1;
	SSL_set_fd(s, sockfd);
	if(SSL_connect(s) <= 0){
		SSL_free(s);
		SSL_CTX_free(c);
		close(sockfd);
		return -1;
	}

	using_tls = 1;
	return sockfd;
}

void cleanup_tls(){
	if(using_tls){
		if(s)SSL_free(s);
		if(c)SSL_CTX_free(c);
	}
}

void show_version(){
	printf("is-%s\n", version);
}

void help(const char *is){
	printf("usage: %s [options]..\n", is);
	printf("options:\n");
	printf("  -n	custom nick, default is %s\n", default_nick);
	printf("  -c	custom channel, default is %s\n", default_channel);
	printf("  -s	custom server, default is %s\n", default_server);
	printf("  -t	custom port, default is %d\n", default_port);
	printf("  -p	server passwd, if required\n");
	printf("  -v	show version information\n");
	printf("  -h	display this\n");
}

int main(int argc, char *argv[]){
	char nick[max_nick_ln + 1];
	char channel[max_input];
	char server[256];
	char passwd[1024] = {0};
	int port = default_port;
	strncpy(nick, default_nick, sizeof(nick) - 1);
	nick[sizeof(nick)-1] = '\0';
	strncpy(channel, default_channel, sizeof(channel) - 1);
	channel[sizeof(channel)-1] = '\0';
	strncpy(server, default_server, sizeof(server) - 1);
	server[sizeof(server)-1] = '\0';
	int opt;
	while((opt = getopt(argc, argv, "n:c:s:t:p:vh")) != -1){
		switch(opt){
			case 'n':
				strncpy(nick, optarg, sizeof(nick) - 1);
				nick[sizeof(nick)-1] = '\0';
				break;
			case 'c':
				strncpy(channel, optarg, sizeof(channel) - 1);
				channel[sizeof(channel)-1] = '\0';
				break;
			case 's':
				strncpy(server, optarg, sizeof(server) - 1);
				server[sizeof(server)-1] = '\0';
				break;
			case 't':
				port = atoi(optarg);
				break;
			case 'p':
				strncpy(passwd, optarg, sizeof(passwd) - 1);
				passwd[sizeof(passwd)-1] = '\0';
				break;
			case 'v':
				show_version();
				return 0;
			case 'h':
			default:
				help(argv[0]);
				return 0;
		}
	}

	get_term_sz();
	int sockfd = (port == 6697) ? connect_to_server_tls(server, port) : connect_to_server(server, port);
	if(sockfd < 0){
		fprintf(stderr, "connection failed\n");
		return 1;
	}

	enable_raw_m();
	atexit(disable_raw_m);
	atexit(cleanup_tls);
	if(passwd[0] != '\0'){
		s_raw(sockfd, "pass %s\r\n", passwd);
	}

	s_raw(sockfd, "nick %s\r\n", nick);
	s_raw(sockfd, "user %s 0 * :%s\r\n", nick, nick);
	s_raw(sockfd, "join %s\r\n", channel);
	clearscrn();
	for(int i = 0; i < max_messages; i++) messages[i] = NULL;
	message_cn = 0;

	char rbuf[bufsz * 4] = {0};
	int rbuf_ln = 0;

	list_messages();
	list_status_line(nick, server, channel);
	list_output(input_buf);
	
	while(1){
		fd_set f;
		FD_ZERO(&f);
		FD_SET(sockfd, &f);
		FD_SET(STDIN_FILENO, &f);
		int maxf = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;
		if(select(maxf + 1, &f, NULL, NULL, NULL) < 0) break;
		if(FD_ISSET(sockfd, &f)){
			int ln = s_recv(sockfd, rbuf + rbuf_ln, sizeof(rbuf) - rbuf_ln - 1);
			if(ln <= 0){
				perror("recv failed");
				break;
			} else if(ln == 0){
				fprintf(stderr, "recv returned 0\n");
				continue;
			}

			rbuf_ln += ln;
			rbuf[rbuf_ln] = '\0';
			char *line_start = rbuf;
			char *line_end;
			while((line_end = strstr(line_start, "\r\n"))){
				*line_end = '\0';
				if(strncmp(line_start, "PING :", 6) == 0){
					s_raw(sockfd, "PING :%s\r\n", line_start + 6);
				} else {
					print_server_line(line_start);
				}

				line_start = line_end + 2;
			}

			rbuf_ln = strlen(line_start);
			memmove(rbuf, line_start, rbuf_ln);
			rbuf[rbuf_ln] = '\0';

			list_status_line(nick, server, channel);
			list_output(input_buf);
			
			fflush(stdout);
		}

		if(FD_ISSET(STDIN_FILENO, &f)){
			char c;
			if(read(STDIN_FILENO, &c, 1) == 1){
				if(c == '\n' || c == '\r'){
					input_buf[input_pos] = '\0';
					if(input_buf[0] == '/'){
						if(!strncmp(input_buf, "/q", 2)){
						printf("\n");
						s_raw(sockfd, "quit: \r\n");
						break;
					} else if(!strncmp(input_buf, "/j ", 3)){
						s_raw(sockfd, "join %s\r\n", input_buf + 3);
						strncpy(channel, input_buf + 3, sizeof(channel) - 1);
						channel[sizeof(channel)-1] = '\0';
						list_status_line(nick, server, channel);
					} else if(!strncmp(input_buf, "/n ", 3)){
						s_raw(sockfd, "nick %s\r\n", input_buf + 3);
						strncpy(nick, input_buf + 3, sizeof(nick) - 1);
						nick[sizeof(nick)-1] = '\0';
						list_status_line(nick, server, channel);
					} else {
						print_server_line("command not found\n");
					}

				} else if(input_buf[0]){
					s_raw(sockfd, "privmsg %s :%s\r\n", channel, input_buf);
					print_user_line(nick, input_buf);
				}

				input_pos = 0;
				input_buf[0] = '\0';

				list_status_line(nick, server, channel);
				list_output(input_buf);
			} else if((c == 127 || c == 8) && input_pos > 0){
				input_buf[--input_pos] = '\0';
				printf("\b \b");
				fflush(stdout);
			} else if(c >= 32 && c <= 126 && input_pos < max_input - 1){
				input_buf[input_pos++] = c;
				input_buf[input_pos] = '\0';
				putchar(c);
				fflush(stdout);
			}

			}
		}
	}

	for(int i = 0; i < message_cn; i++){
		free(messages[i]);
	}

	close(sockfd);

	return 0;
}
