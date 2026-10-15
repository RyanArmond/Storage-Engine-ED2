# Persistent Storage Engine (Entrega 1 - ED2)

Implementação de um motor de armazenamento persistente chave-valor de baixo nível desenvolvido para a disciplina de **Estruturas de Dados 2 (ED2)** em **C++20**.

O motor adota o modelo arquitetural do **Bitcask** (Append-Only Log + KeyDir em memória), com foco estrito em simplicidade, robustez, integridade de dados e recuperação contra falhas (crash recovery).

---

## 1. Arquitetura do Sistema

```
                 +---------------------------------------------+
                 |            Storage Engine (RAM)             |
                 |                                             |
                 |   KeyDir (Hash Map):                        |
                 |   uint64_t key -> {offset, val_sz}          |
                 +---------------------------------------------+
                                     |
                       PUT / DELETE  |  GET (Positional Seek)
                       (Append)      |  (pread @ offset)
                                     v
                 +---------------------------------------------+
                 |            Disco: data.log                  |
                 |   [Header 18B | Value] [Header 18B | Value] |
                 +---------------------------------------------+
```

### 1.1. Disco (Append-Only Log)
Todas as operações de escrita (`PUT`) e deleção (`DELETE` com tombstone) são anexadas sequencialmente ao final de um arquivo binário único (`data.log`).

**Layout do Registro Binário:**
Cada registro possui um cabeçalho fixo de **18 bytes** (em *Little-Endian*) seguido pelos bytes brutos do valor:

| Campo | Tamanho | Descrição |
| :--- | :---: | :--- |
| **Magic** | 1 byte | Byte identificador fixo (`0xED`) |
| **OpType** | 1 byte | Tipo de operação: `0x01` (PUT) ou `0x02` (DELETE) |
| **CRC32** | 4 bytes | Checksum IEEE 802.3 calculado sobre `OpType + Key + ValSize + Payload` |
| **Key** | 8 bytes | Chave numérica `uint64_t` (Little-Endian) |
| **ValSize** | 4 bytes | Tamanho em bytes do valor `uint32_t` (0 para DELETE) |
| **Value** | *N* bytes | Bytes brutos do payload (tamanho *N* = `ValSize`) |

### 1.2. Memória (Índice KeyDir)
Mantém um `std::unordered_map<uint64_t, KeyDirEntry>` em memória primária:
- `record_offset`: Posição absoluta em bytes onde o cabeçalho do registro começa no log.
- `val_offset`: Posição em bytes onde os dados do payload iniciam (`record_offset + 18`).
- `val_size`: Tamanho em bytes do valor persistido.

### 1.3. Recuperação e Crash Resilience
- **Boot Recovery:** Ao iniciar, o motor lê sequencialmente o `data.log` desde o byte `0` até o final do arquivo, recalculando e checando os checksums CRC32 e preenchendo a tabela hash.
- **Torn Writes (Escritas Incompletas por Crash):** Se o sistema for desligado abruptamente durante uma escrita em disco, o leitor de recuperação detecta bytes incompletos ou divergência de CRC no final do log e trunca o arquivo até o último offset válido com `ftruncate`.

---

## 2. Complexidade Assintótica

| Operação | Tempo | I/O de Disco |
| :--- | :---: | :--- |
| **PUT(key, value)** | $O(1)$ amortizado | 1 escrita sequencial (append) + `fdatasync()` |
| **GET(key)** | $O(1)$ | 1 busca direta no log (`pread` posicional) |
| **DELETE(key)** | $O(1)$ amortizado | 1 escrita sequencial de tombstone (18 bytes) |
| **RECOVERY** | $O(N)$ | Leitura sequencial completa do log |

---

## 3. Compilação e Testes

### Pré-requisitos
- Compilador C++ com suporte a **C++20** (`g++ >= 11` ou `clang++ >= 13`).
- Utilitário padrão `make`.
- Nenhuma dependência externa necessária (Zero-dependency).

### Compilar
```bash
make
```
Irá gerar o binário `engine` (e o symlink `storage_engine`).

### Executar a Suíte de Testes
```bash
make test
```

---

## 4. Interface de Linha de Comando (CLI)

O binário aceita quatro comandos fundamentais:

### 4.1. `init`
Inicializa as estruturas no diretório especificado:
```bash
./engine init --data-dir ./meu_db
```

### 4.2. `run`
Executa um workload a partir de um arquivo JSON Lines e grava as respostas em outro arquivo JSON Lines:
```bash
./engine run --data-dir ./meu_db --input entrada.jsonl --output saida.jsonl
```

### 4.3. `verify`
Valida a integridade física do log, checa os checksums CRC32 de cada registro e emite um relatório estatístico:
```bash
./engine verify --data-dir ./meu_db
```

### 4.4. `describe`
Imprime os detalhes arquiteturais do storage engine na saída padrão:
```bash
./engine describe
```

---

## 5. Protocolo JSON Lines

### Entrada (`input`):
```json
{"id": 1, "op": "put", "key": 91, "value": "abc"}
{"id": 2, "op": "get", "key": 91}
{"id": 3, "op": "delete", "key": 91}
{"id": 4, "op": "get", "key": 91}
```

### Saída (`output`):
```json
{"id": 1, "status": "ok"}
{"id": 2, "status": "ok", "value": "abc"}
{"id": 3, "status": "ok"}
{"id": 4, "status": "not_found"}
```
