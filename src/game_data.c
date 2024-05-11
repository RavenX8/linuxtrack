#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <mxml.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <string.h>
#include <utils.h>


//First 5 bytes is MD5 hash of "NaturalPoint"
static uint8_t secret_key[] = {0x0e, 0x9a, 0x63, 0x71, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t S[256] = {0};
static uint8_t rc4_i = 0;
static uint8_t rc4_j = 0;

static char *decoded = NULL;

static mxml_node_t *xml = NULL;
static mxml_node_t *tree = NULL;

static void ksa(uint8_t key[], size_t len)
{
  unsigned int i, j;
  for(i = 0; i < 256; ++i){
    S[i] = i;
  }
  j = 0;
  for(i = 0; i < 256; ++i){
    j = (j + S[i] + key[i % len]) % 256;
    uint8_t tmp = S[i];
    S[i] = S[j];
    S[j] = tmp;
  }
  rc4_i = rc4_j = 0;
}

static uint8_t rc4()
{
  rc4_i += 1;
  rc4_j += S[rc4_i];
  uint8_t tmp = S[rc4_i];
  S[rc4_i] = S[rc4_j];
  S[rc4_j] = tmp;
  return S[(S[rc4_i] + S[rc4_j]) % 256];
}

static bool decrypt_file(const char *fname, bool from_update)
{
  uint32_t header[5];
  size_t datlen;
  ksa(secret_key, 16);
  FILE *inp;
  struct stat fst;

  if((inp = fopen(fname, "rb")) == NULL){
    printf("Can't open input file '%s'", fname);
    return false;
  }

  if(fstat(fileno(inp), &fst) != 0){
    fclose(inp);
    printf("Cannot stat file '%s'\n", fname);
    return false;
  }

  if(from_update){
    if(fread(&header, sizeof(uint32_t), 5, inp) != 5){
      fclose(inp);
      printf("Can't read the header - file '%s' is less than 20 bytes long?\n", fname);
      return false;
    }
    datlen = header[4];
  }else{
    datlen = fst.st_size;
  }
  if((decoded = (char *)malloc(datlen+1)) == NULL){
    printf("malloc failed!\n");
    return false;
  }
  memset(decoded, 0, datlen+1);
  size_t i;
  size_t len = fread(decoded, 1, datlen, inp);
  (void) len;
  for(i = 0; i < datlen; ++i) decoded[i] ^= rc4();
  fclose(inp);

  //inp = fopen("tmp.dump", "w");
  //fwrite(decoded, 1, datlen, inp);
  //fclose(inp);

  return true;
}

static bool game_data_init(const char *fname, bool from_update)
{
  static bool initialized = false;
  if(initialized){
    return true;
  }
  if(!decrypt_file(fname, from_update)){
    printf("Error decrypting file!\n");
    return false;
  }
  xml = mxmlNewXML("1.0");
  tree = mxmlLoadString(xml, decoded, MXML_TEXT_CALLBACK);
  return (tree != NULL);
}

static void game_data_close()
{
  mxmlDelete(tree);
  free(decoded);
}

bool get_game_data(const char *input_fname, const char *output_fname, bool from_update)
{
  FILE *outfile = NULL;
  if((outfile = fopen(output_fname, "w")) == NULL){
    ltr_int_log_message("Can't open the output file '%s'!\n", output_fname);
    return false;
  }
  if(!game_data_init(input_fname, from_update)){
    ltr_int_log_message("Can't process the data file '%s'!\n", input_fname);
    return false;
  }

  mxml_node_t *game;
  const char *name;
  const char *id;
  for(game = mxmlFindElement(tree, tree, "Game", NULL, NULL, MXML_DESCEND);
    game != NULL;
    game =  mxmlFindElement(game, tree, "Game", NULL, NULL, MXML_DESCEND))
  {
    name = mxmlElementGetAttr(game, "Name");
    id = mxmlElementGetAttr(game, "Id");

    mxml_node_t *appid = mxmlFindElement(game, game, "ApplicationID", NULL, NULL, MXML_DESCEND);
    if(appid == NULL){
      fprintf(outfile, "%s \"%s\"\n", id, name);
    }else{
      mxml_node_t *child = mxmlGetFirstChild(appid);
      if(child != NULL){
	      const char *val = mxmlGetElement(child);
        fprintf(outfile, "%s \"%s\" (%s)\n", id, name, val);
      }else{
        fprintf(outfile, "%s \"%s\"\n", id, name);
      }
    }
  }
  fclose(outfile);
  game_data_close();
  return true;
}

