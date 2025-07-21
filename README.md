# is
minimal irc client

# compile
$ clang is.c -o is -Wall -Werror -Os -s -lssl -lcrypto

# usage
$ ./is

$ ./is -n user

$ ./is -c "#channel"

$ ./is -n user -s irc.hackint.org -t 6697 -c "#channel"

# options
```
-n    custom nick, default is user
-c    custom channel, default is #hackint
-s    custom server, default is irc.hackint.org
-t    custom port, default is 6697
-p    server passwd, if required
```

# commands
/q to quit

/j to join a channel

/n to change nick

# features
tls support for 6697 port

messages listing adapts to terminal size

messages shown with timestamps

status line showing current nick, server, channel

# notes
make your own configuration in config.h

default nick is user

default server is irc.libera.chat

default port is 6697, tls

default channel is #hackint

# example
<img width="706" height="604" alt="image" src="https://github.com/user-attachments/assets/5f6fb7de-adff-4517-8f99-b7a163236038" />
