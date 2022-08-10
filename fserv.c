// $ cc fserv.c -o fserv
//
// start a static file server at current directory
// $ ./fserv
//
// specify a port
// $ ./fserv -p 1234

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define REQ_SIZE 1024
#define BODY_SIZE 65536
#define PATH_SIZE 1024
#define DATE_SIZE 32
#define DEF_PORT 8000

struct mime {
	char* ext;
	char* mime;
};

struct mime mimes[] = {
	{ "gif",   "image/gif" },
	{ "jpg",   "image/jpeg" },
	{ "jpeg",  "image/jpeg"},
	{ "png",   "image/png" },
	{ "ico",   "image/ico" },
	{ "svg",   "image/svg+xml" },
	{ "mp3",   "audio/mpeg" },
	{ "aac",   "audio/aac" },
	{ "wav",   "audio/wav" },
	{ "ogg",   "audio/ogg" },
	{ "mid",   "audio/midi" },
	{ "midi",  "audio/midi" },
	{ "mp4",   "video/mp4" },
	{ "htm",   "text/html" },
	{ "html",  "text/html" },
	{ "txt",   "text/plain" },
	{ "otf",   "font/otf" },
	{ "ttf",   "font/ttf" },
	{ "woff",  "font/woff" },
	{ "woff2", "font/woff2" },
	{ "xml",   "application/xml" },
	{ "zip",   "application/zip" },
	{ "pdf",   "application/pdf" },
	{ "json",  "application/json" },
	{ "js",    "application/javascript" },
	{ 0, 0 },
};

void writef(int fd, char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	size_t size = vsnprintf(NULL, 0, fmt, args);
	va_end(args);
	char* buf = malloc(size + 1);
	va_start(args, fmt);
	vsnprintf(buf, size + 1, fmt, args);
	va_end(args);
	write(fd, buf, size);
	free(buf);
}

void writen(int fd, char* msg) {
	write(fd, msg, strlen(msg));
}

bool isfile(char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool isdir(char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int main(int argc, char** argv) {

	int port = DEF_PORT;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
			port = atoi(argv[++i]);
			if (!port || port <= 1024 || port >= 65536) {
				fprintf(stderr, "invalid port: %s\n", argv[i]);
				return EXIT_FAILURE;
			}
		}
	}

	int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (sock_fd == -1) {
		fprintf(stderr, "failed to create socket");
		return EXIT_FAILURE;
	}

	setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, (int[]){1}, sizeof(int));

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr = {
			.s_addr = INADDR_ANY,
		},
		.sin_port = htons(port),
	};

	if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		switch (errno) {
			case EACCES:
				fprintf(stderr, "port %d is in protected", port);
				return EXIT_FAILURE;
			case EADDRINUSE:
				fprintf(stderr, "port %d is in use", port);
				return EXIT_FAILURE;
			default:
				fprintf(stderr, "failed to bind");
				return EXIT_FAILURE;
		}
	}

	listen(sock_fd, 64);

	// start handling requests for infinity
	while (1) {

		int conn_fd = accept(sock_fd, NULL, NULL);
		char req[REQ_SIZE + 1];

		// read request
		read(conn_fd, req, REQ_SIZE);

		// check request
		char* path_start = req + 4;
		char* path_end = strchr(path_start, ' ');
		int path_len = path_end - path_start;

		if (
			strncmp(req, "GET ", 4) != 0
			|| !path_end
			|| path_start[0] != '/'
			|| path_start[1] == '/'
		) {
			writen(conn_fd, "HTTP/1.1 400 Bad Request\r\n\r\n:( 400");
			close(conn_fd);
			continue;
		}

		char path[PATH_SIZE + 1];

		// TODO: handle escaped chars like %20

		// append . in front
		sprintf(path, ".%.*s", (int)(path_end - path_start), path_start);

		// remove trailing /
		if (path_end[-1] == '/') {
			path[path_end - path_start] = '\0';
		}

		// get Date
		time_t rawtime;
		time(&rawtime);
		struct tm* timeinfo = gmtime(&rawtime);
		char datebuf[DATE_SIZE + 1];

		size_t datebuf_len = strftime(
			datebuf,
			DATE_SIZE,
			"%a, %d %b %Y %T GMT",
			timeinfo
		);

		if (isdir(path)) {

			char index_path[PATH_SIZE + 1];
			sprintf(index_path, "%s/index.html", path);

			if (isfile(index_path)) {

				// serve index.html if exists
				strcpy(path, index_path);

			} else {

				// serve dir listing
				DIR* dir = opendir(path);

				if (!dir) {
					writen(conn_fd, "HTTP/1.1 500 Internal Server Error\r\n\r\n:( 500");
					close(conn_fd);
					continue;
				}

				struct dirent* entry;
				char list[BODY_SIZE + 1];
				int len = 0;

				len += sprintf(list + len,
					"<!DOCTYPE html>"
					"<html>"
					"<head>"
						"<title>%s</title>"
						"<style>"
						"* {"
							"margin: 0;"
							"padding: 0;"
						"}"
						"body {"
							"padding: 16px;"
							"font-size: 16px;"
							"font-family: Monospace;"
						"}"
						"li {"
							"list-style: none;"
						"}"
						"a {"
							"color: blue;"
							"text-decoration: none;"
						"}"
						"a:hover {"
							"background: blue;"
							"color: white;"
						"}"
						"</style>"
					"</head>"
					"<body>"
						"<ul>",
					path + 1
				);

				while ((entry = readdir(dir))) {
					if (entry->d_name[0] != '.') {
						char epath[PATH_SIZE + 1];
						sprintf(epath, "%s/%s", path, entry->d_name);
						len += sprintf(
							list + len,
							"<li><a href=\"%s\">%s",
							epath + 1,
							entry->d_name
						);
						if (isdir(epath)) len += sprintf(list + len, "/");
						len += sprintf(list + len, "</a></li>");
					}
				}

				len += sprintf(list + len,
						"</ul>"
					"</body>"
					"</html>"
				);

				// TODO: .fignore
				// TODO: sort dir / file
				// TODO: include .. to go up
				writen(conn_fd,
					"HTTP/1.1 200 OK\r\n"
					"Connection: keep-alive\r\n"
					"Server: fserv\r\n"
				);

				writen(conn_fd, "Content-Length: ");
				writef(conn_fd, "%d", len);
				writen(conn_fd, "\r\n");
				writen(conn_fd, "Content-Type: text/html\r\n");
				writen(conn_fd, "Date: ");
				write(conn_fd, datebuf, datebuf_len);
				writen(conn_fd, "\r\n\r\n");
				write(conn_fd, list, len);

				close(conn_fd);

				continue;

			}

		}

		if (!isfile(path)) {
			writen(conn_fd, "HTTP/1.1 404 Not Found\r\n\r\n:( 404");
			close(conn_fd);
			continue;
		}

		// open requested file
		int file_fd = open(path, O_RDONLY);

		if (file_fd == -1) {
			writen(conn_fd, "HTTP/1.1 500 Internal Server Error\r\n\r\n:( 500");
			close(conn_fd);
			continue;
		}

		// get Content-Length
		long fsize = lseek(file_fd, 0, SEEK_END);
		lseek(file_fd, 0, SEEK_SET);

		// write headers
		writen(conn_fd,
			"HTTP/1.1 200 OK\r\n"
			"Connection: keep-alive\r\n"
			"Server: fserv\r\n"
		);

		writen(conn_fd, "Content-Length: ");
		writef(conn_fd, "%d", fsize);
		writen(conn_fd, "\r\n");
		writen(conn_fd, "Date: ");
		write(conn_fd, datebuf, datebuf_len);
		writen(conn_fd, "\r\n");

		// write Content-Type
		char* ext = strrchr(path, '.');

		if (ext) {
			for (struct mime* m = mimes; m->ext != NULL; m++) {
				if (strcmp(m->ext, ext + 1) == 0) {
					writen(conn_fd, "Content-Type: ");
					writen(conn_fd, m->mime);
					writen(conn_fd, "\r\n");
					break;
				}
			}
		}

		writen(conn_fd, "\r\n");

		// write body
		char body[BODY_SIZE + 1];

		while (1) {
			int ret = read(file_fd, body, BODY_SIZE);
			if (ret <= 0) break;
			write(conn_fd, body, ret);
		}

		// cleanup
		close(file_fd);
		close(conn_fd);

	}

	close(sock_fd);

}
