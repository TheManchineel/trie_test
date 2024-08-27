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
#include <assert.h>
#include <stdint.h>

#define OFFSET_LOWER 0
#define OFFSET_UPPER 26
#define OFFSET_DIGIT 52
#define OFFSET_UNDERSCORE 62

#ifdef METRICS
int numero_ricette = 0;
int numero_aggiunte_ricette = 0;
int numero_comandi_aggiungi_ricetta = 0;
int numero_eliminazioni_ricette = 0;
int numero_trie_nodes = 0;
int numero_aggiunte_ingrediente_nuovo = 0;
int numero_collisioni = 0;
#endif

#ifndef MAX_TRIE_NODES
// As Bill Gates once said, "${MAX_TRIE_NODES} ought to be enough for anybody"
#define MAX_TRIE_NODES 200
#endif

// Prime number for the hash table size
#define RECIPE_HT_BUCKET_COUNT 92233

/* ********************************** TYPE DEFINITIONS **********************************/

typedef enum OrderState
{
  PENDING = 0,
  SHIPPABLE = 1
} OrderState_t;

typedef enum RecipeDeleteResult
{
  RECIPE_DELETED = 0,
  RECIPE_NOT_FOUND = 1,
  RECIPE_HAS_ORDERS = 2
} RecipeDeleteResult_t;

typedef struct __attribute__((packed)) Order
{
  int order_time;
  int order_quantity;
  int order_weight;
  char *recipe_name;
  struct Recipe *recipe;
  struct Order *next_order;
  OrderState_t state;
} Order_t;

typedef struct __attribute__((packed)) IngredientLot
{
  int quantity;
  int expiration_time;
  struct IngredientLot *next_lot;
} IngredientLot_t;

typedef struct __attribute__((packed)) Ingredient
{
  int total_quantity;
  int last_expired_check_time;
  struct IngredientLot *lot_list;
} Ingredient_t;

typedef struct __attribute__((packed)) RecipeIngredient
{
  Ingredient_t *ingredient;
  int quantity;
  struct RecipeIngredient *next_ingredient;
  int uses;
} RecipeIngredient_t;

typedef struct Recipe
{
  int weight;
  int order_count;
  char *name;
  RecipeIngredient_t *ingredients_list;
  struct Recipe *next_recipe;
} Recipe_t;

/* ********************************** TRIE **********************************/

typedef uint32_t trie_id_t;

typedef struct __attribute__((packed)) ChildField
{
  trie_id_t value : 20;
} ChildField_t;

typedef struct __attribute__((packed)) TrieNode
{
  void *dest;
  ChildField_t children[26 * 2 + 10 + 1]; // [a-zA-Z0-9_]
} TrieNode_t;

/* ********************************** HASH TABLE **********************************/

typedef uint64_t Hash_t;

/* ********************************** GLOBAL DECLARATIONS **********************************/

Order_t *order_queue = NULL;
Order_t *order_queue_tail = NULL;

TrieNode_t *trie_node_pool;
TrieNode_t *ingredients_root;

Recipe_t *recipe_ht[RECIPE_HT_BUCKET_COUNT];

int courier_interval;
int courier_capacity;
int current_time = 0;
int shippable_order_count = 0;

/* ********************************** METHODS **********************************/

// Computes the djb2 hash of a string
Hash_t djb2_hash_compute(char *str)
{
  Hash_t hash = 5381; // black magic f*ckery
  int c;
  while ((c = *str++))               // NULL-check, dereference and increment in a trenchcoat
    hash = ((hash << 5) + hash) + c; // hash * 33 + c

  return hash;
}

// Replaces malloc for the trie nodes
trie_id_t trie_malloc()
{
  static int trie_node_pool_alloc = 0;
  if (trie_node_pool_alloc >= MAX_TRIE_NODES)
  {
    puts("Out of trie nodes! Increase MAX_TRIE_NODES. Exiting...");
    exit(1);
  }
#ifdef METRICS
  numero_trie_nodes++;
#endif
  return trie_node_pool_alloc++;
}

// Skips to the end of the line in the file
void go_to_line_end(FILE *file)
{
  while (getchar_unlocked() != '\n' && !feof(file))
    continue;
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
      if (!current_node->children[OFFSET_LOWER + c - 'a'].value)
      {
        if (!create_if_missing)
          return NULL;

        current_node->children[OFFSET_LOWER + c - 'a'].value = trie_malloc();
      }
      current_node = &trie_node_pool[current_node->children[OFFSET_LOWER + c - 'a'].value];
      break;

    case 'A' ... 'Z':
      if (!current_node->children[OFFSET_UPPER + c - 'A'].value)
      {
        if (!create_if_missing)
          return NULL;

        current_node->children[OFFSET_UPPER + c - 'A'].value = trie_malloc();
      }
      current_node = &trie_node_pool[current_node->children[OFFSET_UPPER + c - 'A'].value];
      break;

    case '0' ... '9':
      if (!current_node->children[OFFSET_DIGIT + c - '0'].value)
      {
        if (!create_if_missing)
          return NULL;

        current_node->children[OFFSET_DIGIT + c - '0'].value = trie_malloc();
      }
      current_node = &trie_node_pool[current_node->children[OFFSET_DIGIT + c - '0'].value];
      break;

    case '_':
      if (!current_node->children[OFFSET_UNDERSCORE].value)
      {
        if (!create_if_missing)
          return NULL;

        current_node->children[OFFSET_UNDERSCORE].value = trie_malloc();
      }
      current_node = &trie_node_pool[current_node->children[OFFSET_UNDERSCORE].value];
      break;
    }

    continue;
  end_of_string:
    break;
  }
  return current_node;
}

// Returns the ingredient, creating it if it doesn't exist
Ingredient_t *ingredient_find_or_create(char *key)
{
  TrieNode_t *node = trie_node_find_or_create(ingredients_root, key, true);
  if (!node->dest)
  {
#ifdef METRICS
    numero_aggiunte_ingrediente_nuovo++;
#endif
    node->dest = calloc(sizeof(Ingredient_t), 1);
    // (Implicitly zeroed)
    // ((Ingredient_t *)node->dest)->total_quantity = 0;
  }
  return node->dest;
}

// Deletes a recipe and its ingredients
RecipeDeleteResult_t recipe_delete(char *recipe_name)
{
  Recipe_t *recipe = recipe_ht[djb2_hash_compute(recipe_name) % RECIPE_HT_BUCKET_COUNT];
  Recipe_t **prev_recipe = &recipe_ht[djb2_hash_compute(recipe_name) % RECIPE_HT_BUCKET_COUNT];

  while (recipe && strcmp(recipe->name, recipe_name))
  {
    prev_recipe = &recipe->next_recipe;
    recipe = recipe->next_recipe;
  }

#ifdef METRICS
  numero_ricette--;
  numero_eliminazioni_ricette++;
#endif

  if (!recipe)
    return RECIPE_NOT_FOUND;
  if (recipe->order_count)
    return RECIPE_HAS_ORDERS;

  *prev_recipe = recipe->next_recipe;
  free(recipe->name);
  free(recipe);
  return RECIPE_DELETED;
}

// Returns the recipe, or NULL if it doesn't exist
Recipe_t *recipe_find(char *recipe_name)
{
  Recipe_t *recipe = recipe_ht[djb2_hash_compute(recipe_name) % RECIPE_HT_BUCKET_COUNT];

  while (recipe && strcmp(recipe->name, recipe_name))
    recipe = recipe->next_recipe;

  return recipe;
}

// Adds a new recipe, returning it if it was added and NULL if it already existed
Recipe_t *recipe_add(char *recipe_name)
{
  Hash_t hash = djb2_hash_compute(recipe_name);
  Recipe_t **recipe = &recipe_ht[hash % RECIPE_HT_BUCKET_COUNT];

#ifdef METRICS
  bool already_collided = false;
#endif

  while (*recipe && strcmp((*recipe)->name, recipe_name))
  {
    recipe = &(*recipe)->next_recipe;
#ifdef METRICS
    if (!already_collided)
    {
      numero_collisioni++;
      already_collided = true;
    }
#endif
  }

  if (*recipe)
    return NULL;

  *recipe = calloc(sizeof(Recipe_t), 1);
  (*recipe)->name = strdup(recipe_name);

#ifdef METRICS
  numero_aggiunte_ricette++;
  numero_ricette++;
#endif
  return *recipe;
}

// Adds a new lot to the ingredient (sorted by expiration date)
void ingredient_replenish(TrieNode_t *trie_root, char *key, int quantity, int expiration)
{
  Ingredient_t *ingredient = ingredient_find_or_create(key);
  IngredientLot_t *new_lot = calloc(sizeof(IngredientLot_t), 1);
  new_lot->quantity = quantity;
  new_lot->expiration_time = expiration;
  ingredient->total_quantity += quantity;

  // TODO: use a min-heap instead of a linked list for O(log n) insertion
  if (!ingredient->lot_list)
  {
    ingredient->lot_list = new_lot;
    return;
  }
  else if (ingredient->lot_list->expiration_time > expiration)
  {
    new_lot->next_lot = ingredient->lot_list;
    ingredient->lot_list = new_lot;
    return;
  }
  else
  {
    IngredientLot_t *current = ingredient->lot_list;
    while (current->next_lot && current->next_lot->expiration_time <= expiration)
      current = current->next_lot;
    new_lot->next_lot = current->next_lot;
    current->next_lot = new_lot;
  }
}

// Removes expired lots from the ingredient (if any)
void clear_expired_lots(Ingredient_t *ingredient)
{
  for (
      IngredientLot_t *current_lot = ingredient->lot_list;
      current_lot && current_lot->expiration_time < current_time;
      ingredient->lot_list = current_lot)
  {
    IngredientLot_t *next_lot = current_lot->next_lot;
    ingredient->total_quantity -= current_lot->quantity;
    free(current_lot);
    current_lot = next_lot;
  }
}

// Consumes the ingredients and returns true if the order is shippable, doesn't alter the pantry and returns false otherwise
bool check_and_fill_order(Order_t *order)
{
  Recipe_t *order_recipe = order->recipe;

  for (RecipeIngredient_t *current_recipe_ingredient = order_recipe->ingredients_list; current_recipe_ingredient; current_recipe_ingredient = current_recipe_ingredient->next_ingredient)
  {
    Ingredient_t *current_ingredient = current_recipe_ingredient->ingredient;
    if (current_ingredient->last_expired_check_time != current_time)
    {
      clear_expired_lots(current_ingredient);
      current_ingredient->last_expired_check_time = current_time;
    }
    if (current_ingredient->total_quantity < current_recipe_ingredient->quantity * order->order_quantity)
      return false;
  }

  for (RecipeIngredient_t *current_recipe_ingredient = order_recipe->ingredients_list; current_recipe_ingredient; current_recipe_ingredient = current_recipe_ingredient->next_ingredient)
  {
    int quantity_needed = current_recipe_ingredient->quantity * order->order_quantity;
    Ingredient_t *ingredient = current_recipe_ingredient->ingredient;

    IngredientLot_t **current_lot_ptr = &ingredient->lot_list;
    while (quantity_needed > 0)
    {
      if ((*current_lot_ptr)->quantity <= quantity_needed)
      {
        quantity_needed -= (*current_lot_ptr)->quantity;
        ingredient->total_quantity -= (*current_lot_ptr)->quantity;
        IngredientLot_t *next_lot = (*current_lot_ptr)->next_lot;
        free(*current_lot_ptr);
        *current_lot_ptr = next_lot;
      }
      else
      {
        (*current_lot_ptr)->quantity -= quantity_needed;
        ingredient->total_quantity -= quantity_needed;
        quantity_needed = 0;
      }
    }
  }
  return true;
}

// Evaluates orders in the pending queue and moves them to the shipping queue if they can be fulfilled
void evaluate_pending_orders()
{
  // Order_t *next_order;
  for (Order_t *current_order = order_queue; current_order; current_order = current_order->next_order)
    if (current_order->state == PENDING && check_and_fill_order(current_order))
    {
      current_order->state = SHIPPABLE;
      shippable_order_count++;
    }
}

// Attempts to prepare the order and add it to the shipping queue, or if ingredients are missing, adds it to the pending queue
void add_order(Order_t *new_order)
{
  current_time++; // simulate the accurate expiration time of ingredients
  if (check_and_fill_order(new_order))
  {
    new_order->state = SHIPPABLE;
    shippable_order_count++;
  }
  else
    new_order->state = PENDING;
  current_time--; // restore the accurate current_time

  // insert the order at end of queue
  if (!order_queue)
  {
    order_queue = new_order;
    order_queue_tail = new_order;
  }
  else
  {
    order_queue_tail->next_order = new_order;
    order_queue_tail = new_order;
  }
}

// Comparison function for orders for qsort. Orders are sorted by weight descending, then by time of arrival ascending
int order_cmp(const void *a, const void *b)
{
  Order_t *order_a = *(Order_t **)a;
  Order_t *order_b = *(Order_t **)b;
  if (order_a->order_weight != order_b->order_weight)
    return order_b->order_weight - order_a->order_weight;
  return order_a->order_time - order_b->order_time;
}

// Count shippable orders from the queue, pop them in a dynamically allocated array for qsort() and print them ordered
void courier()
{
  if (!shippable_order_count) // This is not strictly necessary, but it's a good optimization
  {
    puts("camioncino vuoto");
    return;
  }

  // this is an array of pointers to the shippable orders, so we can sort them
  // we need to account for the worst-case scenario, in which actual_shippable_orders == shippable_order_count
  Order_t **shippable_order_array = malloc(sizeof(Order_t *) * shippable_order_count);

  int actual_shippable_orders = 0;
  int remaining_capacity = courier_capacity;

  for (Order_t **current_order = &order_queue; *current_order;)
  {
    if ((*current_order)->state == SHIPPABLE)
    {
      if ((*current_order)->order_weight <= remaining_capacity)
      {
        // reminder to self: val++ evaluates to val, then increments val
        shippable_order_array[actual_shippable_orders++] = *current_order;
        remaining_capacity -= (*current_order)->order_weight;
        if (order_queue_tail == *current_order)
          order_queue_tail = *current_order = NULL;
        else
          *current_order = (*current_order)->next_order;
        shippable_order_count--;
        // we no longer care about the .next_order field
      }
      else
        break;
    }
    else
      // move to the next order
      current_order = &(*current_order)->next_order;
  }

  if (actual_shippable_orders == 0)
  {
    free(shippable_order_array);
    puts("camioncino vuoto");
    return;
  }

  qsort(shippable_order_array, actual_shippable_orders, sizeof(Order_t *), order_cmp);
  for (int i = 0; i < actual_shippable_orders; i++)
  {
    Order_t *current_order = shippable_order_array[i];
    // ⟨istante_di_arrivo_ordine⟩ ⟨nome_ricetta⟩ ⟨numero_elementi_ordinati⟩
    printf("%d %s %d\n", current_order->order_time, current_order->recipe_name, current_order->order_quantity);
    current_order->recipe->order_count--;
    // this is the programming equivalent of the pull-out method of birth control, we almost leaked memory here
    free(current_order);
  }

  free(shippable_order_array);
}

/* **************************************************************************************** */
/*                                      PROGRAM MAIN                                        */
/* **************************************************************************************** */

int main()
{
  trie_node_pool = calloc(sizeof(TrieNode_t), MAX_TRIE_NODES);
  ingredients_root = &trie_node_pool[trie_malloc()];
  char recipe_name[256];
  char token[64];

  FILE *file;
  // file = fopen("/Users/manchineel/archivio_materiali/test_cases_pubblici/open2.txt", "r");
  file = stdin;

  assert(scanf("%d %d ", &courier_interval, &courier_capacity) == 2);

  // MAIN EVENT LOOP ****************************************************************************************
  while (scanf("%s", token) == 1)
  {
    if (!strcmp(token, "aggiungi_ricetta"))
    {
#ifdef METRICS
      numero_comandi_aggiungi_ricetta++;
#endif
      assert(scanf("%s", recipe_name) == 1);
      Recipe_t *recipe = recipe_add(recipe_name);
      if (recipe)
      {
        char ingredient_name[256];
        int ingredient_quantity;
        int total_weight = 0;

        // scan ingredients and append them to the recipe cascadingly
        RecipeIngredient_t **current_ingredient = &recipe->ingredients_list;
        while (getchar_unlocked() != '\n') // always consumes one character, presumably whitespace
        {
          assert(scanf("%s %d", ingredient_name, &ingredient_quantity) == 2); // last whitespace stays in the buffer for getchar_unlocked to read
          total_weight += ingredient_quantity;
          *current_ingredient = calloc(sizeof(RecipeIngredient_t), 1);
          (*current_ingredient)->quantity = ingredient_quantity;
          (*current_ingredient)->ingredient = ingredient_find_or_create(ingredient_name);
          current_ingredient = &(*current_ingredient)->next_ingredient;
        }
        *current_ingredient = NULL;

        recipe->weight = total_weight;
        puts("aggiunta");
      }
      else
      {
        puts("ignorato"); // recipe already exists
        go_to_line_end(file);
      }
    }
    else if (!strcmp(token, "rimuovi_ricetta"))
    {
      assert(scanf("%s", recipe_name) == 1);
      char *result_strings[] = {
          "rimossa",           // recipe_delete(recipe_name) == RECIPE_DELETED
          "non presente",      // recipe_delete(recipe_name) == RECIPE_NOT_FOUND
          "ordini in sospeso", // recipe_delete(recipe_name) == RECIPE_HAS_ORDERS
      };
      puts(result_strings[recipe_delete(recipe_name)]);
    }
    else if (!strcmp(token, "rifornimento"))
    {
      while (getchar_unlocked() != '\n')
      {
        char ingredient_name[256];
        int ingredient_quantity;
        int ingredient_expiration;
        assert(scanf("%s %d %d", ingredient_name, &ingredient_quantity, &ingredient_expiration) == 3);

        ingredient_replenish(ingredients_root, ingredient_name, ingredient_quantity, ingredient_expiration);
      }
      puts("rifornito");
      current_time++;            // replenishments take one time unit
      evaluate_pending_orders(); // new ingredients might make some orders shippable
      current_time--;            // current_time is incremented at the end of the loop
    }
    else if (!strcmp(token, "ordine"))
    {
      Order_t *new_order = malloc(sizeof(Order_t));
      char order_recipe_name[256];
      assert(scanf("%s %d", order_recipe_name, &new_order->order_quantity) == 2);
      Recipe_t *recipe = recipe_find(order_recipe_name);
      if (recipe)
      {
        new_order->recipe = recipe;
        new_order->recipe_name = recipe->name;
        new_order->order_time = current_time;
        new_order->state = PENDING;
        new_order->next_order = NULL;
        add_order(new_order);
        new_order->order_weight = recipe->weight * new_order->order_quantity;
        recipe->order_count++;
        puts("accettato");
      }
      else
      {
        free(new_order);
        puts("rifiutato");
      }
      go_to_line_end(file);
    }

    current_time++;
    if (!(current_time % courier_interval) && current_time)
      courier();
  }

#ifdef METRICS
  printf("Numero ricette finale: %d (%d creazioni, %d eliminazioni, %d aggiungi_ricetta)\nNumero ingredienti aggiunti: %d\nNumero trie nodes (da %lu byte ciascuno): %d, per un totale di %ld KiB\n",
         numero_ricette,
         numero_aggiunte_ricette,
         numero_eliminazioni_ricette,
         numero_comandi_aggiungi_ricetta,
         numero_aggiunte_ingrediente_nuovo,
         sizeof(TrieNode_t),
         numero_trie_nodes,
         numero_trie_nodes * sizeof(TrieNode_t) / 1024);
  printf("Dimensione della tabella hash delle ricette: %ld KiB (%d bucket)\n", RECIPE_HT_BUCKET_COUNT * sizeof(Recipe_t) / 1024, RECIPE_HT_BUCKET_COUNT);
  printf("Numero collisioni nelle aggiunte delle ricette: %d\n", numero_collisioni);
#endif

  return 0;
}

/*****
"L'uso delle parole è essenziale, sensuale, senza un senso sensoriale.
 È tentare di internare in te il male per un test intenzionale."
-- Tedua, "Step by Step" feat. Bresh

"Tu forse hai bisogno di un senso, di un segno improvviso
 Per dire, "okay, oggi sto un po' in paradiso"
 Ho bisogno soltanto di un gesto al mattino
 Di un posto sicuro, uno specchio lontano
 Vedermi un puntino, bloccare il respiro
 Poi prendermi in giro, capire che a volte
 A volte non è perfetto"
-- Angelina Mango, da Tedua - "Paradiso II"
                                                                  *****/