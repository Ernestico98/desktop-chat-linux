# desktop-chat-linux
### Desktop CLI Chat built for Linux in C using Sockets, Threads, Semaphores and Shared memory zones

Run in your terminal to compile

```bash
gcc -c server.c
gcc -o server server.o -pthread
gcc -c client.c
gcc -o client client.o -pthread
```

Run ./server and/or ./client and follow the appearing instructions.

Check the [demo video](https://github.com/Ernestico98/desktop-chat-linux/blob/master/demo-video.mkv) for details and usage demonstration.

