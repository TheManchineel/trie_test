#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifndef NO_METRICS
int numero_ricette = 0;
int numero_aggiunte_ricette = 0;
int numero_comandi_aggiungi_ricetta = 0;
int numero_eliminazioni_ricette = 0;
int numero_trie_nodes = 0;
#endif

typedef struct Ingredient
{
  int quantity;
  int expiration_time;
  struct Ingredient *next_lot;
} Ingredient_t;

typedef struct RecipeIngredient
{
  Ingredient_t **lot_list;
  int quantity;
  struct RecipeIngredient *next_ingredient;
} RecipeIngredient_t;

typedef struct Recipe
{
  int weight;
  int order_count;
  RecipeIngredient_t *ingredients_list;
} Recipe_t;

typedef struct TrieNode
{
  void *dest;
  struct TrieNode *children_lower[26];
  struct TrieNode *children_upper[26];
  struct TrieNode *children_digit[10];
  struct TrieNode *child_underscore;
} TrieNode_t;

// Skips to the end of the line in the file
void go_to_line_end(FILE *file)
{
  while (fgetc(file) != '\n' && !feof(file))
    continue;
}

// Returns a new recipe
Recipe_t *recipe_create()
{
#ifndef NO_METRICS
  numero_ricette++;
  numero_aggiunte_ricette++;
#endif
  Recipe_t *recipe = calloc(sizeof(Recipe_t), 1);
  return recipe;
}

// Returns a new trie node
TrieNode_t *trie_node_create()
{
#ifndef NO_METRICS
  numero_trie_nodes++;
#endif
  TrieNode_t *node = calloc(sizeof(TrieNode_t), 1);
  return node;
}

// Returns the requested node, creating it recursively if it doesn't exist
TrieNode_t *trie_node_find_or_create(TrieNode_t *trie_root, char *key, bool create_if_missing)
{
  TrieNode_t *current_node = trie_root;
  for (unsigned int i = 0; /* condition checked inside loop */; i++)
  {
    char c = key[i];
    switch (c)
    {
    case '\0':
      goto end_of_string;

    case 'a' ... 'z':
      if (!current_node->children_lower[c - 'a'])
      {
        if (!create_if_missing)
          return NULL;

        current_node->children_lower[c - 'a'] = trie_node_create();
      }
      current_node = current_node->children_lower[c - 'a'];
      break;

    case 'A' ... 'Z':
      if (!current_node->children_upper[c - 'A'])
      {
        if (!create_if_missing)
          return NULL;

        current_node->children_upper[c - 'A'] = trie_node_create();
      }
      current_node = current_node->children_upper[c - 'A'];
      break;

    case '0' ... '9':
      if (!current_node->children_digit[c - '0'])
      {
        if (!create_if_missing)
          return NULL;

        current_node->children_digit[c - '0'] = trie_node_create();
      }
      current_node = current_node->children_digit[c - '0'];
      break;

    case '_':
      if (!current_node->child_underscore)
      {
        if (!create_if_missing)
          return NULL;

        current_node->child_underscore = trie_node_create();
      }
      current_node = current_node->child_underscore;
      break;
    }

    continue;
  end_of_string:
    break;
  }
  return current_node;
}

// Returns 1 if actually deleted, 0 if not found
bool trie_destination_delete_if_exists(TrieNode_t *trie_root, char *key)
{
  TrieNode_t *current_node = trie_root;
  for (unsigned int i = 0; /* condition checked inside loop */; i++)
  {
    char c = key[i];

    switch (c)
    {
    case '\0':
      goto end_of_string;
    case 'a' ... 'z':
      if (!current_node->children_lower[c - 'a'])
        return false;
      current_node = current_node->children_lower[c - 'a'];
      break;
    case 'A' ... 'Z':
      if (!current_node->children_upper[c - 'A'])
        return false;
      current_node = current_node->children_upper[c - 'A'];
      break;
    case '0' ... '9':
      if (!current_node->children_digit[c - '0'])
        return false;
      current_node = current_node->children_digit[c - '0'];
      break;
    case '_':
      if (!current_node->child_underscore)
        return false;
      current_node = current_node->child_underscore;
      break;
    }

    continue;

  end_of_string:
    break;
  }

  if (current_node->dest)
  {
    free(current_node->dest);
    current_node->dest = NULL;
    return true;
  }
  return false;
}

int main()
{
  int current_time = 0;
  char recipe_name[256];
  char token[64];

  FILE *file = fopen("/Users/manchineel/Downloads/archivio_materiali/test_cases_pubblici/open11.txt", "r");

  TrieNode_t *recipes_root = trie_node_create();
  TrieNode_t *ingredients_root = trie_node_create(); // not used for now

  while (fscanf(file, "%s", token) == 1)
  {
    if (!strcmp(token, "aggiungi_ricetta"))
    {
#ifndef NO_METRICS
      numero_comandi_aggiungi_ricetta++;
#endif
      fscanf(file, "%s", recipe_name);
      printf("Aggiungi ricetta: %s\n", recipe_name);
      TrieNode_t *recipe_node = trie_node_find_or_create(recipes_root, recipe_name, true);

      if (!recipe_node->dest)
      {
        recipe_node->dest = recipe_create();
        printf("Ricetta %s aggiunta\n", recipe_name);
        // scan ingredients:
        char ingredient_name[256];
        int ingredient_quantity;
        while (true)
        {
          if (fgetc(file) == '\n')
            break;
          fscanf(file, "%s %d", ingredient_name, &ingredient_quantity) == 2;
          printf("Ingredient: %s, quantity: %d\n", ingredient_name, ingredient_quantity);
        }
      }
      else
      {
        printf("Ricetta %s già esistente\n", recipe_name);
      }
    }
    else if (!strcmp(token, "rimuovi_ricetta"))
    {
      fscanf(file, "%s", recipe_name);
      printf("Rimuovi ricetta: %s\n", recipe_name);
      if (trie_destination_delete_if_exists(recipes_root, recipe_name))
      {
#ifndef NO_METRICS
        numero_ricette--;
        numero_eliminazioni_ricette++;
#endif
        printf("Ricetta %s rimossa\n", recipe_name);
      }
      else
      {
        printf("Ricetta %s non trovata\n", recipe_name);
      }
    }
    else
      // we don't need to implement other commands for now
      go_to_line_end(file);
  }

#ifndef NO_METRICS
  char *test_keys[] = {
      "Z81OlQgP8Q",
      "Z81OlQb5e",
  };
  for (unsigned int i = 0; i < sizeof(test_keys) / sizeof(test_keys[0]); i++)
  {
    TrieNode_t *node = trie_node_find_or_create(recipes_root, test_keys[i], false);
    if (node)
    {
      printf("Address of recipe node %s: %p - exists: %s (at %p)\n", test_keys[i], node, node->dest ? "yes" : "no", node->dest);
    }
  }
  printf("Numero ricette finale: %d (%d creazioni, %d eliminazioni, %d aggiungi_ricetta)\nNumero trie nodes (da %lu byte ciascuno): %d, per un totale di %ld KiB\n", numero_ricette, numero_aggiunte_ricette, numero_eliminazioni_ricette, numero_comandi_aggiungi_ricetta, sizeof(TrieNode_t), numero_trie_nodes, numero_trie_nodes * sizeof(TrieNode_t) / 1024);
#endif

  return 0;
}
