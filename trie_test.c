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

int courier_interval;
int courier_capacity;
int current_time = 0;
int shippable_order_count = 0;

#ifdef METRICS
int numero_ricette = 0;
int numero_aggiunte_ricette = 0;
int numero_comandi_aggiungi_ricetta = 0;
int numero_eliminazioni_ricette = 0;
int numero_trie_nodes = 0;
#endif

/* ********************************** TYPE DEFINITIONS **********************************/

typedef enum OrderState
{
  PENDING = 0,
  SHIPPABLE = 1
} OrderState_t;

typedef struct Order
{
  int order_time;
  int order_quantity;
  int order_weight;
  char recipe_name[256];
  struct Recipe *recipe;
  struct Order *next_order;
  OrderState_t state;
} Order_t;

typedef struct IngredientLot
{
  int quantity;
  int expiration_time;
  struct IngredientLot *next_lot;
} IngredientLot_t;

typedef struct Ingredient
{
  int total_quantity;
  struct IngredientLot *lot_list;
} Ingredient_t;

typedef struct RecipeIngredient
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

Order_t *order_queue = NULL;

// Skips to the end of the line in the file
void go_to_line_end(FILE *file)
{
  while (fgetc(file) != '\n' && !feof(file))
    continue;
}

// Returns a new recipe
Recipe_t *recipe_create()
{
#ifdef METRICS
  numero_ricette++;
  numero_aggiunte_ricette++;
#endif
  Recipe_t *recipe = calloc(sizeof(Recipe_t), 1);
  return recipe;
}

// Deletes a recipe and its ingredients
void recipe_delete(Recipe_t *recipe)
{
#ifdef METRICS
  numero_ricette--;
  numero_eliminazioni_ricette++;
#endif
  RecipeIngredient_t *current_ingredient = recipe->ingredients_list;
  while (current_ingredient)
  {
    RecipeIngredient_t *next_ingredient = current_ingredient->next_ingredient;
    free(current_ingredient);
    current_ingredient = next_ingredient;
  }
  free(recipe);
}

// Returns a new trie node
TrieNode_t *trie_node_create()
{
#ifdef METRICS
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

// Returns the ingredient, creating it if it doesn't exist
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

// Adds a new lot to the ingredient (sorted by expiration date)
void ingredient_replenish(TrieNode_t *trie_root, char *key, int quantity, int expiration)
{
  Ingredient_t *ingredient = ingredient_find_or_create(trie_root, key);
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
    clear_expired_lots(current_recipe_ingredient->ingredient);
    if (current_recipe_ingredient->ingredient->total_quantity < current_recipe_ingredient->quantity * order->order_quantity)
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

  // insert the order in the right place in the queue by time of arrival (unique)
  Order_t **current_order = &order_queue;
  while (*current_order && (*current_order)->order_time < new_order->order_time)
    current_order = &(*current_order)->next_order;

  new_order->next_order = *current_order;
  *current_order = new_order;
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

TrieNode_t *recipes_root, *ingredients_root;

/* **************************************************************************************** */
/*                                      PROGRAM MAIN                                        */
/* **************************************************************************************** */

int main()
{
  recipes_root = trie_node_create();
  ingredients_root = trie_node_create();
  char recipe_name[256];
  char token[64];

  FILE *file;
  // file = fopen("/Users/manchineel/archivio_materiali/test_cases_pubblici/open2.txt", "r");
  file = stdin;

  assert(scanf("%d %d ", &courier_interval, &courier_capacity) == 2);

  // MAIN EVENT LOOP ****************************************************************************************
  // TODO: expedite reading commands by only comparing the first character of the token
  while (scanf("%s", token) == 1)
  {
    if (!strcmp(token, "aggiungi_ricetta"))
    {
#ifdef METRICS
      numero_comandi_aggiungi_ricetta++;
#endif
      assert(scanf("%s", recipe_name) == 1);
      TrieNode_t *recipe_node = trie_node_find_or_create(recipes_root, recipe_name, true);

      if (!recipe_node->dest)
      {
        Recipe_t *recipe = recipe_create();
        recipe_node->dest = recipe;

        char ingredient_name[256];
        int ingredient_quantity;
        int total_weight = 0;

        // scan ingredients and append them to the recipe cascadingly
        RecipeIngredient_t **current_ingredient = &recipe->ingredients_list;
        while (fgetc(file) != '\n') // always consumes one character, presumably whitespace
        {
          assert(scanf("%s %d", ingredient_name, &ingredient_quantity) == 2); // last whitespace stays in the buffer for fgetc to read
          total_weight += ingredient_quantity;
          *current_ingredient = calloc(sizeof(RecipeIngredient_t), 1);
          (*current_ingredient)->quantity = ingredient_quantity;
          (*current_ingredient)->ingredient = ingredient_find_or_create(ingredients_root, ingredient_name);
          current_ingredient = &(*current_ingredient)->next_ingredient;
        }
        // last ingredient ->next_ingredient is already NULL from calloc

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
      TrieNode_t *recipe_node = trie_node_find_or_create(recipes_root, recipe_name, false);
      if (recipe_node && recipe_node->dest)
      {
        if (((Recipe_t *)recipe_node->dest)->order_count == 0)
        {
          recipe_delete(recipe_node->dest);
          recipe_node->dest = NULL;
          puts("rimossa");
        }
        else
          puts("ordini in sospeso");
      }
      else
        puts("non presente");
    }
    else if (!strcmp(token, "rifornimento"))
    {
      while (fgetc(file) != '\n')
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
      Order_t *new_order = calloc(sizeof(Order_t), 1);
      assert(scanf("%s %d", new_order->recipe_name, &new_order->order_quantity) == 2);
      TrieNode_t *recipe_node = trie_node_find_or_create(recipes_root, new_order->recipe_name, false);
      if (recipe_node && recipe_node->dest)
      {
        new_order->recipe = recipe_node->dest;
        new_order->order_time = current_time;
        new_order->state = PENDING;
        // new_order->next_order = NULL; // already zeroed by calloc
        add_order(new_order);
        new_order->order_weight = new_order->recipe->weight * new_order->order_quantity;
        new_order->recipe->order_count++;
        puts("accettato");
      }
      else
      {
        free(new_order);
        puts("rifiutato");
      }
      go_to_line_end(file);
    }
    else
    {
      printf("non implementato: %s\n", token);
      go_to_line_end(file);
    }

    current_time++;
    if (current_time % courier_interval == 0 && current_time != 0)
      courier();
  }

#ifdef METRICS
  char *test_keys[] = {
      "Z81OlQgP8Q", // this one should have been deleted
      "Z81OlQb5e",
      "missing", // missing keys in the trie don't even get printed
  };
  for (unsigned int i = 0; i < sizeof(test_keys) / sizeof(test_keys[0]); i++)
  {
    TrieNode_t *node = trie_node_find_or_create(recipes_root, test_keys[i], false);
    if (node)
      printf("Address of recipe node %s: %p - exists: %s (at %p)\n", test_keys[i], node, node->dest ? "yes" : "no", node->dest);
    else
      printf("Recipe node %s not found\n", test_keys[i]);
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