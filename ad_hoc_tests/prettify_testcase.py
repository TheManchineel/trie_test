#!/usr/bin/env python3
import sys

recipe_map = {}
ingredient_map = {}
recipe_counter = ingredient_counter = 0

file = open(sys.argv[1], "r")

for line in file.readlines():
    if line.startswith("#"):
        print(line)

    if line.startswith("aggiungi_ricetta"):
        input_recipe_name = line.split()[1]
        if input_recipe_name not in recipe_map:
            recipe_map[input_recipe_name] = f"ricetta_{str(recipe_counter).zfill(3)}"
            recipe_counter += 1
        print(f"aggiungi_ricetta {recipe_map[input_recipe_name]}", end="")

        ingredient_tokens_list = line.split()[2:]
        for name, quantity in zip(ingredient_tokens_list[::2], ingredient_tokens_list[1::2]):
            if name not in ingredient_map:
                ingredient_map[name] = f"ingrediente_{str(ingredient_counter).zfill(3)}"
                ingredient_counter += 1
            print(f" {ingredient_map[name]} {quantity}", end="")
        print()
    elif line.startswith("ordine"):
        input_recipe_name = line.split()[1]
        if input_recipe_name not in recipe_map:
            recipe_map[input_recipe_name] = f"ricetta_{str(recipe_counter).zfill(3)}"
            recipe_counter += 1
        quantity = line.split()[2]
        print(f"ordine {recipe_map[input_recipe_name]} {quantity}")
    elif line.startswith("rifornimento"):
        print("rifornimento", end="")
        ingredient_tokens_list = line.split()[1:]
        for name, quantity, expiration_date in zip(
            ingredient_tokens_list[::3], ingredient_tokens_list[1::3], ingredient_tokens_list[2::3]
        ):
            if name not in ingredient_map:
                ingredient_map[name] = f"ingrediente_{str(ingredient_counter).zfill(3)}"
                ingredient_counter += 1
            print(f" {ingredient_map[name]} {quantity} {expiration_date}", end="")
        print()
    elif line.startswith("rimuovi_ricetta"):
        input_recipe_name = line.split()[1]
        if input_recipe_name not in recipe_map:
            recipe_map[input_recipe_name] = f"ricetta_{str(recipe_counter).zfill(3)}"
            recipe_counter += 1
        print(f"rimuovi_ricetta {recipe_map[input_recipe_name]}")
    else:
        print(line, end="")
