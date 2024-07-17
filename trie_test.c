/***************************************************
                         _     _                 _
  /\/\   __ _ _ __   ___| |__ (_)_ __   ___  ___| |
 /    \ / _` | '_ \ / __| '_ \| | '_ \ / _ \/ _ \ |
/ /\/\ \ (_| | | | | (__| | | | | | | |  __/  __/ |
\/    \/\__,_|_| |_|\___|_| |_|_|_| |_|\___|\___|_|

***************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/*****
"L'uso delle parole è essenziale, sensuale, senza un senso sensoriale.
// È tentare di internare in te il male per un test intenzionale."
-- Tedua, "Step by Step" feat. Bresh da Orange County Mixtape (2016)
                                                                  *****/

int courier_interval;
int courier_capacity;
int current_time = 0;

#ifndef NO_METRICS
int numero_ricette = 0;
int numero_aggiunte_ricette = 0;
int numero_comandi_aggiungi_ricetta = 0;
int numero_eliminazioni_ricette = 0;
int numero_trie_nodes = 0;
#endif

typedef struct IngredientLot
{
  int quantity;
  int expiration_time;
  struct IngredientLot *next_lot;
} IngredientLot_t;

typedef struct Ingredient
{
  int total_quantity;
  struct IngredientLot_t *lot_list;
} Ingredient_t;

typedef struct RecipeIngredient
{
  Ingredient_t *ingredient;
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

// Returns true if actually deleted, false if not found
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

Ingredient_t *ingredient_find_or_create(TrieNode_t *trie_root, char *key)
{
  TrieNode_t *node = trie_node_find_or_create(trie_root, key, true);
  if (!node->dest)
  {
    node->dest = calloc(sizeof(Ingredient_t), 1);
    // (Implicitly zeroed)
    // ((Ingredient_t *)node->dest)->total_quantity = 0;
  }
  return node->dest;
}

int main()
{
  char recipe_name[256];
  char token[64];

  FILE *file = fopen("/Users/manchineel/Downloads/archivio_materiali/test_cases_pubblici/open11.txt", "r");

  TrieNode_t *recipes_root = trie_node_create();
  TrieNode_t *ingredients_root = trie_node_create();

  fscanf(file, "%d %d ", &courier_interval, &courier_capacity);

  // MAIN EVENT LOOP
  // TODO: expedite reading commands by only comparing the first character of the token
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
        Recipe_t *recipe = recipe_create();
        recipe_node->dest = recipe;
        printf("Ricetta %s aggiunta\n", recipe_name);

        char ingredient_name[256];
        int ingredient_quantity;
        int total_weight = 0;

        // scan ingredients and append them to the recipe cascadingly
        RecipeIngredient_t **current_ingredient = &recipe->ingredients_list;
        while (fgetc(file) != '\n') // always consumes one character, presumably whitespace
        {
          fscanf(file, "%s %d", ingredient_name, &ingredient_quantity); // last whitespace stays in the buffer for fgetc to read

#ifndef NO_METRICS
          printf("Ingredient: %s, quantity: %d\n", ingredient_name, ingredient_quantity);
#endif

          total_weight += ingredient_quantity;
          *current_ingredient = calloc(sizeof(RecipeIngredient_t), 1);
          (*current_ingredient)->quantity = ingredient_quantity;
          (*current_ingredient)->ingredient = ingredient_find_or_create(ingredients_root, ingredient_name);
          current_ingredient = &(*current_ingredient)->next_ingredient;
        }
        // last ingredient ->next_ingredient is already NULL from calloc

        recipe->weight = total_weight;

#ifndef NO_METRICS
        printf("Total weight of recipe %s: %d\nIngredients:\n", recipe_name, total_weight);
        RecipeIngredient_t *current = recipe->ingredients_list;
        while (current)
        {
          printf(" - %p qty %d (%d in pantry)\n", current->ingredient, current->quantity, current->ingredient->total_quantity);
          current = current->next_ingredient;
        }
#endif
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
      // TODO: implement other commands
      go_to_line_end(file);
    current_time++;
  }

#ifndef NO_METRICS
  char *test_keys[] = {
      "Z81OlQgP8Q", // this one should have been deleted
      "Z81OlQb5e",
      "missing", // missing keys in the trie don't even get printed
  };
  for (unsigned int i = 0; i < sizeof(test_keys) / sizeof(test_keys[0]); i++)
  {
    TrieNode_t *node = trie_node_find_or_create(recipes_root, test_keys[i], false);
    if (node)
    {
      printf("Address of recipe node %s: %p - exists: %s (at %p)\n", test_keys[i], node, node->dest ? "yes" : "no", node->dest);
    }
  }
  printf("Numero ricette finale: %d (%d creazioni, %d eliminazioni, %d aggiungi_ricetta)\nNumero trie nodes (da %lu byte ciascuno): %d, per un totale di %ld KiB\n",
         numero_ricette,
         numero_aggiunte_ricette,
         numero_eliminazioni_ricette,
         numero_comandi_aggiungi_ricetta,
         sizeof(TrieNode_t),
         numero_trie_nodes,
         numero_trie_nodes * sizeof(TrieNode_t) / 1024);
#endif

  return 0;
}
