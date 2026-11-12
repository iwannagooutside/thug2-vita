#include "vita_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

// Deux canaux, volontairement redondants :
//
//   1. sceClibPrintf -> canal de debug du kernel -> plugin Cat-A-Log -> TCP.
//      Temps réel, pratique, mais dépend d'un plugin dont l'état sur la
//      console n'est pas sous notre contrôle (la première tentative de boot
//      n'a produit AUCUNE ligne alors que le main tournait).
//
//   2. ux0:data/thug/thug.log, écrit par sceIo et flushé à chaque ligne.
//      Ne dépend d'aucun plugin, se récupère par FTP après coup. C'est le
//      canal de vérité quand le canal 1 est muet ou que le crash est brutal.
//
// Le coût du flush systématique est assumé : un log bufferisé perdu dans un
// crash est exactement le log qu'on voulait lire.

#ifndef VITA_WRITE_ROOT
#define VITA_WRITE_ROOT "ux0:data/thug"
#endif
#define LOG_DIR  VITA_WRITE_ROOT
#define LOG_PATH LOG_DIR "/thug.log"

static SceUID    s_fd = -1;
static SceUInt64 s_t0 = 0;

/* Ecriture sur la carte. Build publique : journal BORNE a 1 Mo par lancement
 * (une erreur repetee a chaque image remplissait la carte) ; au-dela, une
 * ligne le dit et le reste n'est plus ecrit. Le journal du lancement
 * precedent reste dans thug.prev.log (et thug.prev2.log). */
#ifdef THUG_RELEASE
#define JOURNAL_MAX (1024 * 1024)
static unsigned int s_journal_octets = 0;
#endif
static void journal_ecrire(const void *p, int n)
{
    if (s_fd < 0 || n <= 0)
        return;
#ifdef THUG_RELEASE
    if (s_journal_octets >= JOURNAL_MAX)
        return;
    if (s_journal_octets + (unsigned int)n > JOURNAL_MAX) {
        static const char fin[] = "[journal] 1 Mo atteint : la suite de ce lancement n'est pas ecrite\n";
        sceIoWrite(s_fd, fin, sizeof(fin) - 1);
        s_journal_octets = JOURNAL_MAX;
        return;
    }
    s_journal_octets += (unsigned int)n;
#endif
    sceIoWrite(s_fd, p, n);
}

// Compteur de lignes : c'est le pouls du programme, lu par le chien de garde.
static volatile unsigned int s_lines = 0;

/* Tampon d'ecriture : 16 Ko, soit ~200 lignes par acces SD au lieu d'un par
 * ligne. */
static char s_buf[16384];
static int  s_buf_used = 0;
/* Journal SYNCHRONE (enquete de plantage) : chaque ligne part sur la carte
 * des qu'elle est ecrite. Actif si le temoin LOG_DIR/journal_sync existe : en
 * cas de plantage, le tampon de 16 Ko ci-dessus perdait jusqu'a ~150 lignes,
 * et la derniere ligne lue n'etait pas le dernier point atteint. */
static int s_sync = 0;

/* Copie circulaire pour le serveur de debug. Le journal est ecrit par
 * plusieurs threads (audio, chien de garde) : l'anneau est donc protege,
 * contrairement au tampon ci-dessus qui ne l'a jamais ete. */
/* 2 Mo : a 256 Ko, les traces QB d'un chargement chassaient en quelques
 * secondes les lignes qu'on cherchait (confirmation d'« unlock », demarrage
 * des cinematiques). */
#define RING_SIZE (2 * 1024 * 1024)
static char                  s_ring[RING_SIZE];
static volatile unsigned int s_ring_total = 0;   /* octets ecrits depuis le debut */
static SceUID                s_ring_mtx   = -1;

int vita_log_ring_read(unsigned int *pos, char *out, int max)
{
    if (s_ring_mtx < 0)
        return 0;
    sceKernelLockMutex(s_ring_mtx, 1, NULL);
    unsigned int total = s_ring_total;
    unsigned int p     = *pos;
    if (p > total)
        p = total;                              /* « la fin » */
    if (total - p > RING_SIZE)
        p = total - RING_SIZE;                  /* ecrase : plus ancien dispo */
    int n = (int)(total - p);
    if (n > max)
        n = max;
    /* Deux copies au plus (fin puis debut de l'anneau) au lieu d'un modulo
     * par octet : le fil d'ecriture lit ici sous le verrou que VLOG prend. */
    {
        unsigned int o = p % RING_SIZE;
        unsigned int k = RING_SIZE - o;
        if (k > (unsigned int)n)
            k = (unsigned int)n;
        memcpy(out, s_ring + o, k);
        if ((unsigned int)n > k)
            memcpy(out + k, s_ring, (unsigned int)n - k);
    }
    sceKernelUnlockMutex(s_ring_mtx, 1);
    *pos = p + n;
    return n;
}

/* ECRITURE SUR LA CARTE PAR UN FIL DEDIE (thug2-perf).
 *
 * Le tampon de 16 Ko ci-dessus etait vide par le fil qui journalisait, donc
 * le plus souvent par le fil principal, au milieu de la logique : un
 * sceIoWrite de 16 Ko sur la carte (et le sceIoOpen d'inject.txt qui le
 * suivait) prenait de 100 a 450 ms par moments. Releve sur console au menu
 * principal (thug2-c-all, log_gel.txt) : une image de 200 a 450 ms toutes les
 * 0,5 a 1 s, logees n'importe ou dans la logique -- entre deux c-functions
 * d'un meme script (ShouldEndRun -> GameModeEquals : 375 ms), dans
 * SIO::Manager::process_devices (4 a 9 ms par image en moyenne).
 *
 * Desormais VLOG ne fait que formater et copier dans l'anneau (deja tenu pour
 * le serveur de debug) ; ce fil-ci recopie l'anneau sur la carte quand 16 Ko
 * sont en attente, et au plus tard toutes les 250 ms. Le journal sur la carte
 * est meme plus frais qu'avant (250 ms contre 16 Ko). Si le fil prend plus de
 * 2 Mo de retard, la perte est ecrite dans le journal. Le mode synchrone
 * (temoin journal_sync) reste tel quel : une ecriture par ligne, sur le fil
 * appelant. */
#define DISK_SEUIL   (16 * 1024)
#define DISK_PERIODE 250000                     /* us */
static SceUID                s_disk_mtx   = -1;
static SceUID                s_disk_sema  = -1;
static volatile int          s_writer     = 0;  /* fil d'ecriture actif */
static unsigned int          s_disk_pos   = 0;  /* octets de l'anneau deja sur la carte */
static unsigned int          s_signal_pos = 0;  /* sous s_ring_mtx */

/* Appelant : s_disk_mtx tenu. */
static void disk_drain_locked(void)
{
    static char buf[32 * 1024];
    while (s_fd >= 0) {
        unsigned int pos = s_disk_pos;
        int n = vita_log_ring_read(&pos, buf, sizeof(buf));
        if (n <= 0)
            break;
        const unsigned int debut = pos - (unsigned int)n;
        if (debut != s_disk_pos) {
            char m[96];
            int k = snprintf(m, sizeof(m), "[journal] %u octets perdus (ecriture en retard)\n",
                             debut - s_disk_pos);
            if (k > 0)
                journal_ecrire( m, k);
        }
        journal_ecrire( buf, n);
        s_disk_pos = pos;
    }
}

static int vita_log_writer_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        SceUInt attente = DISK_PERIODE;
        sceKernelWaitSema(s_disk_sema, 1, &attente);
        sceKernelLockMutex(s_disk_mtx, 1, NULL);
        disk_drain_locked();
        sceKernelUnlockMutex(s_disk_mtx, 1);
    }
    return 0;
}

static void vita_log_writer_start(void)
{
    if (s_ring_mtx < 0 || s_fd < 0)
        return;
    s_disk_mtx  = sceKernelCreateMutex("thug_log_disk", 0, 0, NULL);
    s_disk_sema = sceKernelCreateSema("thug_log_disk", 0, 0, 1, NULL);
    if (s_disk_mtx < 0 || s_disk_sema < 0)
        return;
    /* Priorite sous celle des fils audio et du serveur de debug (0x10000100) :
     * il passe surtout son temps bloque dans sceIoWrite. Hors du coeur 0. */
    SceUID th = sceKernelCreateThread("thug_log_disk", vita_log_writer_thread,
                                      0x10000100 + 0x10, 0x4000, 0,
                                      SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL);
    if (th < 0)
        return;
    s_disk_pos = s_ring_total;
    s_writer = 1;
    if (sceKernelStartThread(th, 0, NULL) < 0)
        s_writer = 0;
}

void vita_log_flush(void)
{
    if (s_writer) {
        sceKernelLockMutex(s_disk_mtx, 1, NULL);
        disk_drain_locked();
        sceKernelUnlockMutex(s_disk_mtx, 1);
        return;
    }
    if (s_fd >= 0 && s_buf_used > 0)
    {
        journal_ecrire( s_buf, s_buf_used);
        s_buf_used = 0;
    }
}

void vita_log_init(void)
{
    s_t0 = sceKernelGetProcessTimeWide();
    s_ring_mtx = sceKernelCreateMutex("thug_log_ring", 0, 0, NULL);

    sceIoMkdir(LOG_DIR, 0777);
    // Troncature à l'ouverture : le log d'un run ne se mélange pas au
    // précédent, sinon on debugge le run d'avant sans le savoir.
    // Journal du lancement precedent garde sous thug.prev.log : apres un
    // plantage, relancer le jeu ecrasait le seul temoin (issue C8,
    // thug2-c8-boost). thug2-c8b : deux niveaux (thug.prev.log, puis
    // thug.prev2.log) : un plantage survit a deux relances, par exemple la
    // relance qui suit le plantage puis celle d'un essai pour le reproduire.
    // Destination retiree avant chaque sceIoRename, comme avant.
    sceIoRemove(LOG_DIR "/thug.prev2.log");
    sceIoRename(LOG_DIR "/thug.prev.log", LOG_DIR "/thug.prev2.log");
    sceIoRemove(LOG_DIR "/thug.prev.log");
    sceIoRename(LOG_PATH, LOG_DIR "/thug.prev.log");
    s_fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    {
        SceIoStat st;
#ifndef THUG_RELEASE	/* build publique : aucun temoin de dev lu */
        s_sync = ( sceIoGetstat(LOG_DIR "/journal_sync", &st) >= 0 );
#else
        (void)st;
#endif
    }
    if (s_sync)
        vita_log_printf("SYS", "!! journal SYNCHRONE (temoin journal_sync) : une ecriture par ligne, fait chuter les i/s");
    else
        vita_log_writer_start();
}

// ---------------------------------------------------------------------------
// Chien de garde.
//
// Un moteur fige sans plantage ne laisse RIEN : pas de psp2core, donc pas de
// pile, donc rien a lire. Or le dump de la Vita contient le PC de TOUS les
// threads. Il suffit donc qu'un thread annexe se saborde pour obtenir la
// position exacte du thread principal.
//
// Declenchement : aucune ligne de log ecrite depuis WATCHDOG_TIMEOUT_S.
// C'est un OUTIL DE DEBUG, pas un mecanisme de production -- il transforme
// deliberement un gel silencieux en plantage lisible.
// ---------------------------------------------------------------------------
#ifndef THUG_RELEASE	/* build publique : pas de chien de garde (il plante le jeu expres) */
#define WATCHDOG_TIMEOUT_S 45

static int vita_watchdog_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;

    unsigned int last  = 0;
    int          still = 0;

    // Controle positif : sans cette ligne, l'absence de reaction du chien de
    // garde serait ambigue (thread mort ? ou rien a signaler ?).
    vita_log_printf("WDOG", "chien de garde demarre (seuil %d s)",
                    WATCHDOG_TIMEOUT_S);

    for (;;) {
        sceKernelDelayThread(5 * 1000 * 1000);

        // Empeche la mise en veille automatique.
        //
        // Sans cela, la console suspend le PROCESSUS ENTIER pendant les longs
        // chargements sans interaction. Vu du PC c'est indiscernable d'un
        // blocage : le log s'arrete net, son horodatage vient du temps
        // processus qui n'avance plus, et meme ce thread-ci cesse de tourner.
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);

        unsigned int now = s_lines;
        if (now != last) {
            last  = now;
            still = 0;
            continue;
        }

        still += 5;
        if (still < WATCHDOG_TIMEOUT_S)
            continue;

        vita_log_printf("WDOG", "aucune ligne depuis %d s -- dump provoque",
                        still);
        vita_log_flush();
        // Le dump qui suit contiendra le PC du thread principal : c'est tout
        // l'interet de la manoeuvre.
        *(volatile int *)0 = 0;
    }
    return 0;
}

void vita_log_start_watchdog(void)
{
    SceUID th = sceKernelCreateThread("thug_watchdog", vita_watchdog_thread,
                                      0x10000100, 0x4000, 0, 0, NULL);
    if (th >= 0)
        sceKernelStartThread(th, 0, NULL);
}
#endif /* !THUG_RELEASE */

void vita_log_shutdown(void)
{
    if (s_writer) {
        sceKernelLockMutex(s_disk_mtx, 1, NULL);
        disk_drain_locked();
        if (s_fd >= 0)
            sceIoClose(s_fd);
        s_fd = -1;
        sceKernelUnlockMutex(s_disk_mtx, 1);
        return;
    }
    if (s_fd >= 0) {
        vita_log_flush();
    sceIoClose(s_fd);
        s_fd = -1;
    }
}


extern int g_vita_log_bavard;	/* defini dans p_siodev.cpp (compile aussi par le harnais hote) */

static int log_est_bavard(const char *sys)
{
    static const char *const s_bavards[] = { "2D", "MAT", "LUM", "TXT", "POS", "MLP", "OMB", "GLSPY", "CAM", "PIC", NULL };
    for (int i = 0; s_bavards[i]; ++i)
        if (!strcmp(sys, s_bavards[i]))
            return 1;
    return 0;
}
#ifdef THUG_RELEASE
/* Build public : seules les lignes utiles a un rapport de bug (demarrage,
 * erreurs "!!", plantages) ; le reste n'est meme pas formate. */
static int vita_log_retenu(const char *sys, const char *fmt)
{
    static const char *garder[] = { "SYS", "BOOT", "GFX", "WDOG", "ERR", "ASSERT", "CRASH", "LVL", NULL };
    if (fmt && fmt[0] == '!' && fmt[1] == '!')
        return 1;
    for (int i = 0; garder[i]; ++i)
        if (sys && !strcmp(sys, garder[i]))
            return 1;
    return 0;
}
#endif

void vita_log_printf(const char *sys, const char *fmt, ...)
{
#ifdef THUG_RELEASE
    if (!vita_log_retenu(sys, fmt))
        return;
#endif
    char body[512];
    char line[640];
    va_list ap;

    if (!g_vita_log_bavard && log_est_bavard(sys))
        return;

    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    SceUInt64 ms = (sceKernelGetProcessTimeWide() - s_t0) / 1000;
    int n = snprintf(line, sizeof(line), "[%5llu.%03llu] [%s] %s\n",
                     (unsigned long long)(ms / 1000),
                     (unsigned long long)(ms % 1000), sys, body);
    if (n < 0)
        return;
    if (n > (int)sizeof(line) - 1)
        n = (int)sizeof(line) - 1;

    /* Ecriture TAMPONNEE.
     *
     * Chaque ligne coutait deux appels systeme : un sceClibPrintf vers le port
     * de debogage (que personne n'ecoute en general) et un sceIoWrite sur la
     * carte SD. Mesure pendant un chargement de niveau : 4009 lignes de traces
     * QB a elles seules, soit ~8000 appels systeme et autant d'acces SD --
     * plus de la moitie du temps de chargement passait la.
     *
     * On accumule donc, et on ne touche la carte que par blocs. Le tampon est
     * vide a la fermeture (vita_log_close) et des qu'il est plein ; en cas de
     * plantage on perd au pire les dernieres lignes, ce qui est le compromis
     * habituel -- et le chien de garde, lui, force un vidage. */
    if (s_fd >= 0 && s_sync)
        journal_ecrire( line, n);
    else
    if (s_fd >= 0 && !s_writer)
    {
        if (s_buf_used + n > (int)sizeof(s_buf))
        {
            journal_ecrire( s_buf, s_buf_used);
            s_buf_used = 0;
        }
        if (n <= (int)sizeof(s_buf))
        {
            memcpy(s_buf + s_buf_used, line, n);
            s_buf_used += n;
        }
        else
        {
            journal_ecrire( line, n);
        }
    }

    if (s_ring_mtx >= 0)
    {
        sceKernelLockMutex(s_ring_mtx, 1, NULL);
        unsigned int t = s_ring_total;
        {
            unsigned int o = t % RING_SIZE;
            unsigned int k = RING_SIZE - o;
            if (k > (unsigned int)n)
                k = (unsigned int)n;
            memcpy(s_ring + o, line, k);
            if ((unsigned int)n > k)
                memcpy(s_ring, line + k, (unsigned int)n - k);
        }
        s_ring_total = t + n;
        int reveil = 0;
        if (s_writer && (t + n - s_signal_pos >= DISK_SEUIL)) {
            s_signal_pos = t + n;
            reveil = 1;
        }
        sceKernelUnlockMutex(s_ring_mtx, 1);
        if (reveil)
            sceKernelSignalSema(s_disk_sema, 1);
    }

    ++s_lines;
}
