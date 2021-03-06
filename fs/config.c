/*
 * Copyright 2018 Chris Hooper <amiga@cdh.eebugs.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted so long as any redistribution retains the
 * above copyright notice, this condition, and the below disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

const char *version = "\0$VER: BFFSFileSystem 1.6 (08-Feb-2018) ? Chris Hooper";

/* cache.c */
int cache_size       = 32;   /* initial cache frags if 0 in mountlist */
int cache_cg_size    = 4;    /* number of cylinder group fsblock buffers */

/* dir.c */
int case_dependent   = 0;    /* 1=always case dependent dir name searches */

/* file.c */
int resolve_symlinks = 0;    /* 1=always resolve sym links for AmigaDOS */
int unix_paths       = 0;    /* 1=always Unix pathnames 0=AmigaDOS pathnames */
int read_link        = 0;    /* 1=attempt to resolve links for OS (sneaky) */

/* handler.c */
int timer_secs       = 1;    /* delay seconds after write for cleanup */
int timer_loops      = 12;   /* maximum delays before forced cleanup */
