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

int courier_interval;
int courier_capacity;
int current_time = 0;

#ifdef METRICS
int numero_ricette = 0;
int numero_aggiunte_ricette = 0;
int numero_comandi_aggiungi_ricetta = 0;
int numero_eliminazioni_ricette = 0;
int numero_trie_nodes = 0;
#endif

typedef struct Order
{
  int order_time;
  int order_quantity;
  int order_weight;
  char recipe_name[256];
  struct Recipe *recipe;
  struct Order *next_order;
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

Order_t *pending_queue = NULL;
Order_t *shipping_queue = NULL;

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
  new_lot->next_lot = ingredient->lot_list;
  ingredient->total_quantity += quantity;

  // TODO: use a min-heap instead of a linked list for O(log n) insertion
  for (IngredientLot_t **current = &ingredient->lot_list; *current; current = &(*current)->next_lot)
  {
    if ((*current)->expiration_time > expiration)
    {
      new_lot->next_lot = *current;
      *current = new_lot;
      return;
    }
  }
}

// Evaluates orders in the pending queue
void evaluate_pending_orders()
{
  // lazily traverse the shipping queue once for all iterations, only if any orders are now shippable
  Order_t *shipping_queue_tail = NULL;
  bool shipping_queue_tail_already_resolved = false;

  // we need to check if each order in the pending queue can be shipped, and if so, make it consuming ingredient lots in order of expiration and moving it to the shipping queue
  for (Order_t **current_order = &pending_queue; *current_order;)
  {
    Order_t *order = *current_order;
    Recipe_t *recipe = order->recipe;

    // check if all RecipeIngredient_t's are available
    RecipeIngredient_t *current_ingredient;
    for (current_ingredient = recipe->ingredients_list; current_ingredient; current_ingredient = current_ingredient->next_ingredient)
    {
      Ingredient_t *ingredient = current_ingredient->ingredient;
      if (ingredient->total_quantity < current_ingredient->quantity)
        break;
    }

    if (!current_ingredient) // if we reached the end of the list (NULL), all ingredients are available
    {
      for (current_ingredient = recipe->ingredients_list; current_ingredient; current_ingredient = current_ingredient->next_ingredient)
      {
        Ingredient_t *ingredient = current_ingredient->ingredient;
        int quantity_needed = current_ingredient->quantity;
        ingredient->total_quantity -= quantity_needed; // we can already safely subtract the quantity needed
        IngredientLot_t **current_lot = &ingredient->lot_list;

        while (quantity_needed > 0)
        {
          if ((*current_lot)->quantity <= quantity_needed)
          {
            quantity_needed -= (*current_lot)->quantity;
            IngredientLot_t *next_lot = (*current_lot)->next_lot;
            free(*current_lot);
            *current_lot = next_lot;
          }
          else
          {
            (*current_lot)->quantity -= quantity_needed;
            quantity_needed = 0;
          }
        }
      }

      // move the order to the shipping queue
      if (shipping_queue_tail_already_resolved)
      {
        shipping_queue_tail->next_order = order;
        shipping_queue_tail = order;
      }
      else
      {
        if (shipping_queue) // if the shipping queue is not empty
          for (shipping_queue_tail = shipping_queue; shipping_queue_tail->next_order; shipping_queue_tail = shipping_queue_tail->next_order)
            continue;
        else // this is the first shippable order
        {
          shipping_queue = order;
          shipping_queue_tail = order;
        }
        shipping_queue_tail_already_resolved = true;
      }

      *current_order = order->next_order;
    }
    else
    {
      current_order = &order->next_order;
    }
  }
}

// Attempts to prepare the order and add it to the shipping queue, or if ingredients are missing, adds it to the pending queue
void add_order(Order_t *new_order)
{
  Recipe_t *recipe = new_order->recipe;
  RecipeIngredient_t *current_ingredient;
  Order_t **target_queue;

  for (current_ingredient = recipe->ingredients_list; current_ingredient; current_ingredient = current_ingredient->next_ingredient)
  {
    Ingredient_t *ingredient = current_ingredient->ingredient;
    if (ingredient->total_quantity < current_ingredient->quantity)
      break;
  }
  if (!current_ingredient) // if we reached the end of the list (NULL), all ingredients are available
  {
    target_queue = &shipping_queue;

    for (current_ingredient = recipe->ingredients_list; current_ingredient; current_ingredient = current_ingredient->next_ingredient)
    {
      Ingredient_t *ingredient = current_ingredient->ingredient;
      int quantity_needed = current_ingredient->quantity;
      ingredient->total_quantity -= quantity_needed; // we can already safely subtract the quantity needed
      IngredientLot_t **current_lot = &ingredient->lot_list;

      while (quantity_needed > 0)
      {
        if ((*current_lot)->quantity <= quantity_needed)
        {
          quantity_needed -= (*current_lot)->quantity;
          IngredientLot_t *next_lot = (*current_lot)->next_lot;
          free(*current_lot);
          *current_lot = next_lot;
        }
        else
        {
          (*current_lot)->quantity -= quantity_needed;
          quantity_needed = 0;
        }
      }
    }
  }
  else
    target_queue = &pending_queue;

  // append the order to the end of the target queue
  if (*target_queue)
  {
    Order_t *current_order;
    for (current_order = *target_queue; current_order->next_order; current_order = current_order->next_order)
      continue;
    current_order->next_order = new_order;
  }
  else
    *target_queue = new_order;
}

// STILL TO BE IMPLEMENTED
void courier()
{
  // TBI
  puts("camioncino vuoto"); // TODO: implement courier
}

/* **************************************************************************************** */

int main()
{
  char recipe_name[256];
  char token[64];

  FILE *file = fopen("/Users/manchineel/archivio_materiali/test_cases_pubblici/example.txt", "r");

  TrieNode_t *recipes_root = trie_node_create();
  TrieNode_t *ingredients_root = trie_node_create();

  fscanf(file, "%d %d ", &courier_interval, &courier_capacity);

  // MAIN EVENT LOOP ****************************************************************************************
  // TODO: expedite reading commands by only comparing the first character of the token
  while (fscanf(file, "%s", token) == 1)
  {

    if (!strcmp(token, "aggiungi_ricetta"))
    {
#ifdef METRICS
      numero_comandi_aggiungi_ricetta++;
#endif
      fscanf(file, "%s", recipe_name);
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
          fscanf(file, "%s %d", ingredient_name, &ingredient_quantity); // last whitespace stays in the buffer for fgetc to read
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
        puts("ignorato"); // recipe already exists
    }
    else if (!strcmp(token, "rimuovi_ricetta"))
    {
      fscanf(file, "%s", recipe_name);
      TrieNode_t *recipe_node = trie_node_find_or_create(recipes_root, recipe_name, false);
      if (recipe_node && recipe_node->dest)
      {
        if (((Recipe_t *)recipe_node->dest)->order_count == 0)
        {
          free(recipe_node->dest);
          recipe_node->dest = NULL;
          puts("rimossa");
        }
        else
          puts("ordini in sospeso");
#ifdef METRICS
        numero_ricette--;
        numero_eliminazioni_ricette++;
#endif
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
        fscanf(file, "%s %d %d", ingredient_name, &ingredient_quantity, &ingredient_expiration);

        ingredient_replenish(ingredients_root, ingredient_name, ingredient_quantity, ingredient_expiration);
      }
      puts("rifornito");
      evaluate_pending_orders(); // new ingredients might make some orders shippable
    }
    else if (!strcmp(token, "ordine"))
    {
      Order_t *new_order = calloc(sizeof(Order_t), 1);
      fscanf(file, "%s %d", new_order->recipe_name, &new_order->order_quantity);
      new_order->recipe = trie_node_find_or_create(recipes_root, new_order->recipe_name, false)->dest;
      if (new_order->recipe)
      {
        new_order->order_time = current_time;
        // new_order->next_order = NULL; // already zeroed by calloc
        add_order(new_order);
        new_order->order_weight = new_order->recipe->weight;
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
    // TODO: implement other commands
    current_time++;

    if (current_time % courier_interval == 0 && current_time != 0)
      courier();
  }

  if (current_time % courier_interval != 0)
    courier(); // last courier run

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