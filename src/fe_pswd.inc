/* POLISH: password screen, a port of TITLE.DLL FUN_1000f460 (screen process) and FUN_1000d910 (Meg object,
 * with input handlers FUN_1000d720 facing right / FUN_1000d4b0 facing left). Included by frontend.c.
 * Meg walks in, introduces the screen (speech clip 0), stands between two pillars facing a vase; Left/Right
 * turn or walk to the next gap, Up/Down (or Circle/Square held) hit the vase so it turns to the next picture,
 * Start/Cross checks the code, Triangle leaves. Tables are read from TITLE.DLL. */

#define T_MWALK 0x100257e8u   /* int[] x step (16.16) per walk frame */
#define T_MTURNL 0x10025860u  /* turn to face left (+0x1c) */
#define T_MTURNR 0x10025878u  /* turn to face right (+0x1c) */
#define T_MHITD 0x10025900u   /* hit, vase turns down (+0xa6) */
#define T_MSTEP 0x10025950u   /* start walking (+0xa6) */
#define T_MFID0 0x10025968u   /* fidget: turn to camera (+0x20, 999 = sound id follows) */
#define T_MFID1 0x100259b8u   /* fidget loop */
#define T_MFID2 0x100259d0u   /* fidget: turn back */

static const int PV_X[4] = {-0x55, -0x12, 0x2d, 0x6d};           /* vases a278 / a27c / a280 / a274 */
static const int PV_LO[4] = {0x1e8, 0x126, 0x187, 0xc5}, PV_HI[4] = {0x23f, 0x17d, 0x1de, 0x11c};
static const int PV_BASE[4] = {0x1e3, 0x121, 0x182, 0xc0};

static int pw_vy[4], pw_vb[4], pw_a26c, pw_a258, pw_a394, pw_a398, pw_cheat[3];

static int mstep(FrontEnd *F, int frame) { return frame <= 0 ? 0 : (int)pe_u32(&F->title, T_MWALK + 4u * (uint32_t)frame); }
static int tlen(FrontEnd *F, uint32_t t) { int n = 0; while (n < 64 && tbl(F, t, n) != -1) n++; return n; }
static int speaking(FrontEnd *F) { return F->speech_len - F->speech_t > 0; }

static void pass_enter(FrontEnd *F)
{
    if (F->title_sounds != 1) fe_title_sounds(F, 0);
    F->mode = FE_PASSWORD;
    F->mstate = 0; F->msub = 0; F->midx = 0; F->mcount = 0; F->mx = -0xbc; F->mmirror = 0; F->mframe = 2; F->mret = 4;
    F->mhitdone = 0; F->pa28 = 1;
    pw_a26c = 0; pw_a258 = 0; pw_a394 = 0; pw_a398 = 0xf;
    memset(pw_cheat, 0, sizeof pw_cheat);
    for (int i = 0; i < 4; i++) {
        F->vase_frame[i] = PV_BASE[i] + 24;
        if (F->gs.level >= 0 && F->mcode >= 0 && F->mcode < 42)   /* gamestruct[0x12]: the last password entered */
            F->vase_frame[i] = (int)pe_u32(&F->title, T_PSWD + 0x18u * (uint32_t)F->mcode + 4u * (uint32_t)i) + PV_BASE[i];
        pw_vy[i] = 0; pw_vb[i] = 0;
    }
}

static int pass_check(FrontEnd *F)   /* FUN_1000cd30: entry code, or -1 */
{
    for (int i = 0; i < 42; i++) {
        uint32_t e = T_PSWD + 0x18u * (uint32_t)i;
        int ok = 1;
        for (int v = 0; v < 4; v++) if ((int)pe_u32(&F->title, e + 4u * (uint32_t)v) != F->vase_frame[v] - PV_BASE[v]) ok = 0;
        if (!ok) continue;
        int code = (int)pe_u32(&F->title, e + 16);
        if (code == 100) { pw_cheat[0] = 1; return code; }
        if (code == 101 && pw_cheat[0]) { F->gs.cheats |= 0x80000000u; F->gs.cheat_mode = 0; return code; }
        if (code == 102) { pw_cheat[1] = 1; return code; }
        if (code == 103 && pw_cheat[1]) { F->gs.cheats |= 0x80000000u; F->gs.cheat_mode = 1; return code; }
        if (code == 104) { pw_cheat[2] = 1; return code; }
        if (code == 105 && pw_cheat[2]) { F->gs.cheats |= 0x80000008u; F->gs.demo = 3; return 1; }
        if (code >= 100) return code;
        F->difficulty = F->gs.difficulty = (int)pe_u32(&F->title, e + 20);
        F->mcode = i;
        return code;
    }
    return -1;
}

static void vase_turn(FrontEnd *F, int v, int dir)
{
    F->vase_frame[v] += dir;
    if (F->vase_frame[v] < PV_LO[v]) F->vase_frame[v] = PV_HI[v];
    if (F->vase_frame[v] > PV_HI[v]) F->vase_frame[v] = PV_LO[v];
    int off = F->vase_frame[v] - PV_BASE[v];
    for (int i = 0; i < 11; i++) if (VASE_STOP[v][i] == off) F->mhitdone = 1;   /* switchD_1000e1f7: stop on a picture */
}

static void pass_hit(FrontEnd *F, int st, int ret)
{
    F->mret = ret; F->midx = 0; F->mstate = st; F->mhitdone = 0;
    speech_stop(F);
    fe_snd(F, 0x4311);
}

static void pass_input(FrontEnd *F, int facing_left)   /* FUN_1000d720 / FUN_1000d4b0 */
{
    uint16_t pr = F->pressed;
    if (pr & (PSX_START | PSX_CROSS)) {
        int c = pass_check(F);
        if (c == -1) F->mstate = 0x14;
        else if (c < 100) { F->presult = c; F->mstate = 0x18; }
        else fe_snd(F, 0x302);
    }
    if (pr & PSX_LEFT) {
        if (!facing_left && F->pa28) { F->midx = 0; F->mstate = 0xf; speech_stop(F); fe_snd(F, 0x301); }
        else if (facing_left) { speech_stop(F); F->midx = 0; F->mstate = F->pa28 ? 10 : 0x1a; F->mret = 5; fe_snd(F, 0x301); }
    }
    if (pr & PSX_RIGHT) {
        speech_stop(F); F->midx = 0;
        if (F->pa28 < 3) F->mstate = facing_left ? 0xc : 0x12;
        else { F->mret = facing_left ? 5 : 4; F->mstate = 0x1b; }
        fe_snd(F, 0x300);
    }
    if (pr & PSX_UP) pass_hit(F, 9, facing_left ? 5 : 4);
    if (pr & PSX_DOWN) pass_hit(F, 8, facing_left ? 5 : 4);
}

static void pass_lipsync(FrontEnd *F, int base) { F->mframe = mouth(F) + base; F->speech_t++; }

static void meg_tick(FrontEnd *F)   /* FUN_1000d910 */
{
    int n;
    switch (F->mstate) {
    case 0: F->mx += mstep(F, F->mframe) >> 16; if (++F->mframe == 0x16) { F->mframe = 9; F->mstate = 1; } break;
    case 1: F->mx += mstep(F, F->mframe) >> 16; if (++F->mframe == 0x1d) { F->mframe = 0xa7; F->mstate = 2; } break;
    case 2: if (++F->mframe == 0xab) { speech(F, 0); F->mstate = 3; } break;
    case 3:
        pass_lipsync(F, 0x2d);
        if (!speaking(F)) { speech_stop(F); F->midx = tlen(F, T_PIDLE) - 1; F->mcount = 0; F->mstate = 4; }
        break;
    case 4: case 5: {
        int left = F->mstate == 5;
        if (!left) F->mx = F->pa28 == 1 ? -0x4d : F->pa28 == 2 ? -0xc : F->pa28 == 3 ? 0x35 : F->mx;
        else F->mx = F->pa28 == 0 ? -0x1e : F->pa28 == 1 ? 0x23 : F->pa28 == 2 ? 0x63 : F->mx;
        F->mmirror = left;
        if (tbl(F, T_PIDLE, F->midx) != -1) { F->mframe = tbl(F, T_PIDLE, F->midx) + 0xa6; F->midx++; F->mcount = 0; }
        if (++F->mcount == (left ? 10 : 0x3c)) { F->mframe = 0x21; F->mstate = left ? 7 : 6; F->msub = 0; F->midx = 0; }
        pass_input(F, left);
        break;
    }
    case 6: case 7: {
        int left = F->mstate == 7;
        switch (F->msub) {
        case 0: case 1: {
            uint32_t t = F->msub ? T_MFID1 : T_MFID0;
            F->mframe = tbl(F, t, F->midx) + 0x20; F->midx++;
            while (tbl(F, t, F->midx) == 999) { fe_snd(F, tbl(F, t, F->midx + 1)); F->midx += 2; }
            if (tbl(F, t, F->midx) == -1) { if (F->msub == 0) { F->msub = 1; F->midx = 0; F->mcount = 0; } else F->midx = 1; }
            if (F->msub == 1 && ++F->mcount > 0x3b) { F->msub = 4; F->midx = 0; }
            break;
        }
        case 4:
            F->mframe = tbl(F, T_MFID2, F->midx) + 0x20;
            if (tbl(F, T_MFID2, ++F->midx) == -1) { F->msub = 2; F->midx = 0; }
            break;
        case 2: {   /* FUN_1000d6c0: the five hint clips in turn */
            static int hint;
            speech(F, 1 + hint);
            hint = (hint + 1) % 5;
            F->msub = 3;
            break;
        }
        case 3:
            pass_lipsync(F, 0x2d);
            if (!speaking(F)) { speech_stop(F); F->msub = 0; F->mcount = 0; F->midx = 0; }
            break;
        }
        pass_input(F, left);
        break;
    }
    case 8: case 9: {
        uint32_t t = F->mstate == 8 ? T_MHITD : T_PHITU;
        if (tbl(F, t, F->midx) == -1 || F->mhitdone) {
            F->midx = tlen(F, T_PIDLE) - 1; F->mstate = F->mret; F->mcount = 0; F->mhitdone = 0;
            if (F->held & (PSX_CIRCLE | PSX_SQUARE)) {
                int c = (F->held & PSX_CIRCLE) != 0;
                F->midx = 0;
                F->mstate = !F->mmirror ? (c ? 9 : 8) : (c ? 8 : 9);
                fe_snd(F, 0x4311);
            }
            break;
        }
        F->mframe = tbl(F, t, F->midx) + 0xa6;
        if ((++F->midx & 1) == 0) {
            int dir = (F->mstate == 9) ^ F->mmirror ? 1 : -1;
            vase_turn(F, F->pa28, dir);
        }
        break;
    }
    case 10: case 0xc: case 0xf: case 0x12: case 0x1a: case 0x1b:
        n = tbl(F, T_MSTEP, F->midx);
        if (n != -1) { F->mframe = n + 0xa6; F->midx++; break; }
        if (F->mstate == 10) { F->mstate = 0xb; F->mframe = 2; }
        else if (F->mstate == 0xc) { F->mstate = 0xd; F->mframe = 0xc; }
        else if (F->mstate == 0xf) { F->mstate = 0x10; F->mframe = 0xc; }
        else if (F->mstate == 0x12) { F->mstate = 0x13; F->mframe = 2; }
        else { F->mstate = F->mret; F->midx = tlen(F, T_PIDLE) - 1; F->mcount = 0; }   /* bump at the end */
        break;
    case 0xb:
        F->mx -= mstep(F, ++F->mframe) >> 16;
        if (F->mframe == 0x1a) F->pa28--;
        if (F->mframe == 0x1d) { F->midx = 0; F->mstate = 5; F->mcount = 0; }
        break;
    case 0xd:
        F->mx -= mstep(F, ++F->mframe) >> 16;
        if (F->mframe == 0x1d) { F->midx = 0; F->mstate = 0xe; }
        break;
    case 0xe:
        F->mmirror = 0;
        if (tbl(F, T_MTURNR, F->midx) != -1) { F->mframe = tbl(F, T_MTURNR, F->midx) + 0x1c; F->midx++; }
        else { F->mstate = 4; F->midx = 0; F->pa28++; }
        break;
    case 0x10:
        F->mx += mstep(F, ++F->mframe) >> 16;
        if (F->mframe == 0x1d) { F->midx = 0; F->mstate = 0x11; }
        break;
    case 0x11:
        if (tbl(F, T_MTURNL, F->midx) != -1) { F->mframe = tbl(F, T_MTURNL, F->midx) + 0x1c; F->midx++; }
        else { F->mstate = 5; F->midx = 0; F->pa28--; }
        break;
    case 0x13:
        F->mx += mstep(F, ++F->mframe) >> 16;
        if (F->mframe == 0x1a) F->pa28++;
        if (F->mframe == 0x1d) { F->midx = 0; F->mstate = 4; F->mcount = 0; }
        break;
    case 0x14: {    /* wrong password: one of five remarks, then back to the menu */
        static const int R[5] = {7, 0x14, 6, 0x15, 0x13};
        F->mcode = -1;
        speech(F, R[rand_n(F, 5)]);
        F->mstate = 0x17;
        break;
    }
    case 0x17:
        pass_lipsync(F, 0x2d);
        if (!speaking(F) && pw_a394 != 5) { speech_stop(F); pw_a394 = 5; }
        break;
    case 0x18: {    /* right password: a remark about the level, the vases rise and glow */
        int c = F->presult, s = -1;
        switch (c) {
        case 1: case 10: case 11: s = 9; break;
        case 2: s = 10; break;
        case 3: s = 0xc; break;
        case 5: s = 0x12; break;
        case 6: s = 0xe; break;
        case 7: s = 8; break;
        case 8: s = 0xf; break;
        case 9: s = 0x10; break;
        case 12: s = 0x11; break;
        }
        if (s >= 0) speech(F, s);
        F->msub = 0; F->mcount = 0; F->mframe = 0x33; F->mstate = 0x19;
        break;
    }
    case 0x19:
        if (speaking(F)) pass_lipsync(F, 0x3c); else speech_stop(F);
        for (int v = 0; v < 4 && v < F->msub; v++) {
            int t = F->mcount + (F->msub - 1 - v) * 0x20;
            pw_vy[v] = t < 0x10 ? -(t * (t + 1) / 2) / 16 : -8 + (int)(3.0 * sin(t * 0.39));
            if (++F->vase_frame[v] > PV_HI[v]) F->vase_frame[v] = PV_LO[v];
            pw_vb[v] += (0x80 - pw_vb[v]) >> 1;
            if (rand_n(F, 3) == 0) {
                FeSparkle *s2 = NULL;
                for (int i = 0; i < 48; i++) if (!F->spk[i].active) { s2 = &F->spk[i]; break; }
                if (s2) { s2->active = 1; s2->frame = 0x23 + rand_n(F, 3); s2->x = PV_X[v] + rand_n(F, 0x18) - 0xc; s2->y = -2 + pw_vy[v] - rand_n(F, 0x30); s2->bright = 0x80; s2->rot = 0; }
            }
        }
        if (F->msub < 4 || F->mcount <= 0x20) {
            if (F->msub == 0 || ++F->mcount > 0x20) {
                if (F->msub < 4) { F->msub++; F->mcount = 0; fe_snd(F, 0x4316); }
            }
        } else if (!speaking(F) && pw_a394 == 1) pw_a394 = 2;
        break;
    }
}

static void pass_tick(FrontEnd *F)   /* FUN_1000f460 state 1 */
{
    switch (pw_a394) {
    case 0: if (pw_a26c < 0x80) pw_a26c += 0x10; else { pw_a394 = 1; pw_a26c = 0x80; } break;
    case 2: {
        char n[64]; snprintf(n, sizeof n, "PSWD\\LEV%d.TGA", F->presult);
        free(F->lev_bg); F->lev_bg = tga_load(F, n);
        pw_a394 = 3; pw_a258 = 0;
        break;
    }
    case 3:
        if (pw_a26c > 0) pw_a26c -= 0x10;
        if (pw_a258 < 0x80) pw_a258 += 0x10;
        else if (++F->ptimer > 0x3c) { pw_a394 = 4; F->ptimer = 0; }
        break;
    case 4:
        if (pw_a258 < 1) {   /* state 10: start the level the code names (gamestruct[0] = code - 1, state 0x29) */
            F->gs.level = F->presult - 1;
            F->gs.start_lives = F->cfg && F->cfg->lives > 0 ? F->cfg->lives : 3;
            F->gs.lives = F->gs.start_lives; F->gs.continues = 0;
            F->acec = 1;
            speech_stop(F);
            flow_run(F, 0x29);
            return;
        }
        pw_a258 -= 4;
        break;
    case 5:
        if (pw_a26c < 1) { pw_a26c = 0; speech_stop(F); F->entered = 1; fe_goto_menu(F); return; }
        pw_a26c -= 8;
        break;
    }
    for (int i = 0; i < 48; i++) {
        FeSparkle *s = &F->spk[i];
        if (!s->active) continue;
        if (s->bright <= 0) { s->active = 0; continue; }
        s->bright -= 8; s->y -= 1;
    }
    if (F->held == 0) pw_a398 = 0;
    if (pw_a398) { pw_a398--; return; }
    if ((F->held & PSX_TRIANGLE) && pw_a394 != 5) { fe_snd(F, 0x303); speech_stop(F); pw_a394 = 5; return; }
    if (pw_a394 <= 1) meg_tick(F);
}

static void pass_draw(FrontEnd *F)
{
    memset(F->cv, 0, sizeof F->cv);
    int b = pw_a26c;
    draw_bg(F, F->pswd_bg, b, 0, 0);
    draw_spr(F, frame(&F->pswd, 1), 0x11, 0x6d, b, 2, 0, 0);       /* "USE MEG TO ..." banner + shadow */
    draw_spr(F, frame(&F->pswd, 1), 0x10, 0x6c, b, 1, 0, 0);
    for (int v = 0; v < 4; v++) {
        int vb = F->mstate == 0x19 ? b * (0x80 + pw_vb[v]) >> 7 : b;
        draw_spr(F, frame(&F->pswd, F->vase_frame[v]), PV_X[v], -2 + pw_vy[v], vb, -1, 0, 0);
    }
    for (int i = 47; i >= 0; i--) {
        FeSparkle *s = &F->spk[i];
        if (s->active) draw_spr(F, frame(&F->pswd, s->frame), s->x, s->y, s->bright, 1, 0, 0);
    }
    draw_spr(F, frame(&F->pswd, F->mframe), F->mx, 0x5d, b, -1, F->mmirror, 0);
    if (pw_a394 >= 3 && F->lev_bg) {
        for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++)
            F->cv[y * CW + x] = psx_px(F->cv[y * CW + x], F->lev_bg[y * CW + x], pw_a258, 1);
        for (int v = 0; v < 4; v++)
            draw_spr(F, frame(&F->pswd, F->vase_frame[v]), PV_X[v], -2 + pw_vy[v], pw_a258 * 2, 1, 0, 0);
    }
}
