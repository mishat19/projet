# TODO Daemon + CLI (C / Linux)

Petit projet: un daemon qui gère une base SQLite de tâches et des clients CLI.

Build

```sh
make
```

Run daemon (creates todo.db in cwd):

```sh
bin/todo-daemon
```

Client usage examples:

```sh
bin/todo-client add "Buy milk"
bin/todo-client list
bin/todo-client subscribe   # blocks and prints notifications
bin/todo-client delete 1
```

Systemd unit is provided at `systemd/todo.service` as an example.
