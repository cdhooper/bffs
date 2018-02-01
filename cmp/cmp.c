/*
 * cmp
 *
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
 *
 * Simple file compare utility.
 */

const char *version = "\0$VER: cmp 1.0 (19-Jan-2018) © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>

#define CMP_BLOCK 512

static void
print_usage(const char *prog)
{
    fprintf(stderr, "%s %.3s by Chris Hooper on %.11s\n"
	    "\n"
	    "This program performs a binary comparison of two files\n"
	    "Usage: %s <file1> <file2>\n"
	    "     Exit code will be zero if the files are identical\n",
	    prog, version + 11, version + 16, prog);
    exit(1);
}

static int
cmp(const char *file1, const char *file2)
{
    FILE *fp1 = fopen(file1, "r");
    FILE *fp2 = fopen(file2, "r");
    char *buf1;
    char *buf2;
    size_t size1;
    size_t size2;
    int rc;

    if ((fp1 = fopen(file1, "r")) == NULL) {
	fprintf(stderr, "Failed to open %s\n", file1);
	if ((fp2 = fopen(file2, "r")) == NULL)
	    return (0);
	fclose(fp2);
	return (-1);
    }
    if ((fp2 = fopen(file2, "r")) == NULL) {
	fclose(fp1);
	fprintf(stderr, "Failed to open %s\n", file2);
	return (1);
    }
    buf1 = malloc(CMP_BLOCK);
    buf2 = malloc(CMP_BLOCK);
    do {
	size1 = fread(buf1, sizeof (buf1), 1, fp1);
	size2 = fread(buf2, sizeof (buf2), 1, fp2);
	if (size1 == 0)
	    break;
	while (size1 > 0) {
	    if (size2 <= 0)
		break;
	    size1--;
	    size2--;
	    if (buf1[size1] != buf2[size2]) {
		rc = (buf1[size1] - buf2[size2]);
		goto cleanup;
	    }
	}
    } while (size1 == size2);

    rc = size1 - size2;
cleanup:
    free(buf1);
    free(buf2);
    fclose(fp1);
    fclose(fp2);
    if (rc != 0)
	printf("files differ\n");
    return (rc);
}

int
main(int argc, char *argv[])
{
    if ((argc < 3) || (argc > 3))
	print_usage(argv[0]);
    exit(cmp(argv[1], argv[2]) ? 20 : 0);
}
