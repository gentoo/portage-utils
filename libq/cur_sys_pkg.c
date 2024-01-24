#include "config.h"

#include <assert.h>
#include <openssl/evp.h>
#include <sys/stat.h> 
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <xalloc.h>

#include "contents.h"
#include "md5.h"
#include "hash.h"
#include "cur_sys_pkg.h"


#define HASH_SIZE (MD5_DIGEST_SIZE << 1)

//private

//data
typedef struct cur_pkg_tree_node {
  char *key;
  char *hash_buffer;
  struct cur_pkg_tree_node *greater;
  struct cur_pkg_tree_node *minor;
}cur_pkg_tree_node;


//functions
static void add_node(cur_pkg_tree_node **root,char *data,char *key)
{
  if(*root==NULL)
  {
    *root=xmalloc(sizeof(**root));
    (*root)->key=key;
    (*root)->hash_buffer=data;
    (*root)->greater=NULL;
    (*root)->minor=NULL;
    return;
  }

  int is_greater=strncmp(key,(*root)->key,HASH_SIZE);
  
  if(!is_greater){
    printf("you are reading the same file twice, check CONTENTS file\n");
  }
  
  if(is_greater > 0){
    return add_node(&(*root)->greater,data,key);
  }
  return add_node(&(*root)->minor,data,key);
}

static char *hash_from_file(char *file_path_complete)
{
  char *out = NULL;
  int fd = open(file_path_complete,O_RDONLY);
  out=hash_file_at(fd,file_path_complete,HASH_MD5);
  close(fd);
  return strdup(out);
}

static int find_in_tree(cur_pkg_tree_node **root,char * key,char *hash)
{
  if(!strcmp(hash,"-1")) return 1;

  if((*root) != NULL)
  { 
    int is_greater=strncmp(key,(*root)->key,HASH_SIZE);
    
    if(is_greater == 0){
      return !strcmp(hash,(*root)->hash_buffer); 
    }else if(is_greater < 0){
      return find_in_tree(&(*root)->minor,key,hash);
    }else {
      return find_in_tree(&(*root)->greater,key,hash);
    }
  }
  return 0;
}

//public
int create_cur_pkg_tree(cur_pkg_tree_node **root,struct tree_pkg_ctx *pkg_ctx)
{ 
  char *buf, *savep, *key;   
  contents_entry *e;

  buf = tree_pkg_meta_get(pkg_ctx, CONTENTS);

  for (; (buf = strtok_r(buf, "\n", &savep)) != NULL; buf = NULL) {

		e = contents_parse_line(buf);
		if (!e || e->type != CONTENTS_OBJ){
			continue;
        }
        
      key=hash_from_string(e->name, (size_t) ((e->digest-1)- e->name), HASH_MD5);

      add_node(root,strdup(e->digest),strdup(key));
      key=NULL;
  }
  assert(*root);
  return 0;
}

int is_default(cur_pkg_tree_node *root,char *file_path_complete)
{
  char *key;
  int res=0;
  char *hash =NULL;

  hash = hash_from_file(file_path_complete);
  key= hash_from_string(file_path_complete,strlen(file_path_complete),HASH_MD5);
  res = find_in_tree(&root,key,hash);

  free(hash);
  key=NULL;
  hash=NULL;

  return res;
}

void destroy_cur_pkg_tree(cur_pkg_tree_node **root)
{
  
  if((*root)!=NULL)
  {
    destroy_cur_pkg_tree(&(*root)->greater);
    destroy_cur_pkg_tree(&(*root)->minor);

    free((*root)->hash_buffer);
    (*root)->hash_buffer=NULL;

    free((*root)->key);
    (*root)->key=NULL;
    
    free((*root));
    (*root)=NULL;

    *root = NULL;
  }
}

void in_order_visit(cur_pkg_tree_node *root)
{
  if(root!=NULL)
  {
    if(root->minor!=NULL) in_order_visit(root->minor);
    printf("[%s,%s]\n",root->key,root->hash_buffer);
    if(root->greater!=NULL) in_order_visit(root->greater);
  }
}
