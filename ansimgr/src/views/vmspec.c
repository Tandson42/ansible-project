#include "app.h"
#include "tui/theme.h"
#include "tui/term.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * VM view. The status block is read-only and cheap (procfs only); the two
 * mutating actions delegate to the same pty executor so their output lands in
 * the log with everything else.
 */

static void vm_actions(App *a, Buf *b, int x, int y, int w, int h);

void vm_draw(App *a, Buf *b, int x, int y, int w, int h)
{
    int yy = y;
    buf_puts(b, yy++, x, COL_ACCENT, BG_DEFAULT, A_BOLD, "MAQUINA VIRTUAL QEMU");
    yy++;

    const char *state = vmstat_summary(&a->vm);
    uint8_t sc = COL_OK;
    if (strcmp(state, "desligada") == 0) sc = COL_DIM;
    else if (strcmp(state, "iniciando") == 0) sc = COL_WARN;
    else if (strcmp(state, "porta ocupada") == 0) sc = COL_ERR;

    buf_box(b, yy, x, 12, w, COL_FRAME, "STATUS");
    int iy = yy + 1;
    int ix = x + 2;
    draw_kv(b, iy++, ix, w - 4, "estado", state, sc);

    char v[64];
    if (a->vm.qemu_pid > 0) {
        snprintf(v, sizeof(v), "pid %d", a->vm.qemu_pid);
        draw_kv(b, iy++, ix, w - 4, "processo", v, COL_TEXT);
    } else {
        draw_kv(b, iy++, ix, w - 4, "processo", "nao encontrado", COL_DIM);
    }

    char up[32];
    fmt_duration(up, sizeof(up), a->vm.uptime_s);
    draw_kv(b, iy++, ix, w - 4, "uptime", up, COL_TEXT);

    draw_kv(b, iy++, ix, w - 4, "porta 2222", a->vm.ssh_listening ? "em LISTEN" : "fechada",
            a->vm.ssh_listening ? COL_OK : COL_DIM);
    draw_kv(b, iy++, ix, w - 4, "porta 8080", a->vm.http_listening ? "em LISTEN" : "fechada",
            a->vm.http_listening ? COL_OK : COL_DIM);

    char conn[160];
    snprintf(conn, sizeof(conn), "%s@%s -p 2222", a->vm.ssh_user, a->vm.ssh_host);
    draw_kv(b, iy++, ix, w - 4, "conexao", conn, COL_ACCENT);
    yy += 13;

    /* Port forwarding map, mirroring the -netdev in start_vm.sh. */
    if (w > 40 && h > 16) {
        buf_box(b, yy, x, 5, w, COL_FRAME, "ENCAMINHAMENTO DE PORTAS");
        buf_printf(b, yy + 1, x + 2, 10, COL_DIM, BG_DEFAULT, A_NONE, "host");
        buf_printf(b, yy + 1, x + 16, 10, COL_DIM, BG_DEFAULT, A_NONE, "destino");
        buf_printf(b, yy + 1, x + 32, 20, COL_DIM, BG_DEFAULT, A_NONE, "uso");

        buf_printf(b, yy + 2, x + 2, 10, a->vm.ssh_listening ? COL_OK : COL_DIM,
                   BG_DEFAULT, a->vm.ssh_listening ? A_BOLD : A_NONE, "2222");
        buf_printf(b, yy + 2, x + 16, 10, COL_TEXT, BG_DEFAULT, A_NONE, "VM :22");
        buf_printf(b, yy + 2, x + 32, 20, COL_DIM, BG_DEFAULT, A_NONE, "SSH / Ansible");

        buf_printf(b, yy + 3, x + 2, 10, a->vm.http_listening ? COL_OK : COL_DIM,
                   BG_DEFAULT, a->vm.http_listening ? A_BOLD : A_NONE, "8080");
        buf_printf(b, yy + 3, x + 16, 10, COL_TEXT, BG_DEFAULT, A_NONE, "VM :8080");
        buf_printf(b, yy + 3, x + 32, 20, COL_DIM, BG_DEFAULT, A_NONE, "site container");
        yy += 6;
    }

    vm_actions(a, b, x, yy, w, h - (yy - y));
}

static void vm_actions(App *a, Buf *b, int x, int y, int w, int h)
{
    if (h < 4) return;
    buf_box(b, y, x, h, w, COL_FRAME, "ACOES");
    int iy = y + 1;
    struct { const char *key; const char *text; } rows[4] = {
        { "s",  "subir a VM  (executa scripts/start_vm.sh)" },
        { "k",  "derrubar a VM  (SIGTERM no processo QEMU)" },
        { "r",  "reconsultar o status" },
        { "F5", "testar conectividade com ansible -m ping" },
    };
    for (int i = 0; i < 4 && iy < y + h - 1; i++) {
        buf_printf(b, iy, x + 2, 4, COL_KEY, BG_DEFAULT, A_BOLD, "%s", rows[i].key);
        buf_printf(b, iy, x + 6, w - 8, COL_TEXT, BG_DEFAULT, A_NONE, "%s", rows[i].text);
        iy++;
    }

    if (run_is_busy(a))
        buf_printf(b, y + h - 1, x + 2, w - 4, COL_WARN, BG_DEFAULT, A_NONE,
                   "em execucao: %s", a->job_title);
}

void vm_key(App *a, Key k)
{
    switch (k.type) {
    case KEY_F5: {
        char *argv[8];
        int i = 0;
        argv[i++] = (char *)"ansible";
        argv[i++] = (char *)"all";
        argv[i++] = (char *)"-m";
        argv[i++] = (char *)"ping";
        argv[i] = NULL;
        run_exec(a, RUN_KIND_PING, "ping de conectividade", argv);
        return;
    }
    case KEY_CHAR:
        switch (k.ch[0]) {
        case 's': {
            if (run_is_busy(a)) { app_status(a, 2, "ja existe um comando em execucao"); return; }
            char *argv[4];
            argv[0] = (char *)"bash";
            argv[1] = a->proj.start_script;
            argv[2] = NULL;
            argv[3] = NULL;
            run_exec(a, RUN_KIND_NONE, "iniciar VM QEMU", argv);
            return;
        }
        case 'k': {
            if (a->vm.qemu_pid <= 0) {
                app_status(a, 2, "nenhum processo QEMU deste projeto");
                return;
            }
            snprintf(a->pending_arg, sizeof(a->pending_arg), "%d", a->vm.qemu_pid);
            a->pending_confirm = PC_KILL_VM;
            char msg[256];
            snprintf(msg, sizeof(msg), "Enviar SIGTERM ao QEMU pid %d?", a->vm.qemu_pid);
            confirm_begin(&a->confirm, "derrubar VM", msg);
            return;
        }
        case 'r':
            vmstat_probe(&a->vm, a->proj.disk, a->proj.inventory);
            app_status(a, 1, "status reconsultado: %s", vmstat_summary(&a->vm));
            return;
        default:
            return;
        }
    default:
        return;
    }
}
