#include "parser.h"
#include <stdio.h>
#include "type_checker.h"
#include <stdlib.h>
#include <string.h>

// TODO: Break this code into other files (not all functions here are directly related to statements)

node_t* parse_arr_idx(parser_t* parser) {
  token_t* name = parser->token;
  // FIXME: v~~~~~ Fix this for arrays inside of structure type.
  /*object_t* obj = parser_find_obj(parser, parse_str(name));
  if (!obj->type->is_arr && !obj->type->is_pointer) {
    parser_error(parser, "Invalid indexing of non-array object.");
    return NULL;
  }*/
  list_t* idx_list = list_create();
  int count = 0;
  while (parser_peek(parser)->type == TOK_LSQBR) {
    count++;
    parser_eat(parser, TOK_LSQBR);
    // FIXME: What if its a double pointer ?! or pointer to array ?!?!?!?
    /*if (count > obj->type->dimensions) {
      parser_error(parser, "Trying to access inexistent dimension.");
      return NULL;
    }*/
    parser_consume(parser);
    node_t* expr = parse_expression(parser, NULL);
    parser_expect(parser, TOK_RSQBR);
    list_add(idx_list, expr);
  }
  node_t* node = NEW_DATA(node_t);
  node->lhs = NULL; node->rhs = NULL;
  node->type = NODE_ARR_IDX;
  node->tok = name;
  node->data = idx_list;
  return node;
}

// Returns a node (array) with list of expressions
// Array = '[' { expression | array ',' } ']'
node_t* parse_array(parser_t* parser, type_t* type) {
  node_t* expr;
  node_t* node = NEW_DATA(node_t);
  node->type = NODE_ARRAY;
  list_t* list = list_create();
  while (parser->token->type != TOK_RSQBR) {
    if (type->pointer && type->is_arr) {
      if (type->pointer->arr_size->type == NODE_INT && list->size + 1 > type->pointer->arr_size->value) {
        parser_error(parser, "Too many items in array.");
        return NULL;
      }
    }
    if (parser->token->type == TOK_LSQBR) {
      parser_consume(parser);
      if (type->pointer->pointer && type->pointer->is_arr)
        expr = parse_array(parser, type->pointer);
      else
        expr = parse_array(parser, type);
    } else {
      expr = parse_expression(parser, type);
    }
    list_add(list, expr);
    switch (parser->token->type) {
      case TOK_COMMA:
        parser_consume(parser);
        break;
      case TOK_RSQBR:
        break;
      default:
        parser_error(parser, "Unexpected token. Expected ',' or ']' but got '%.*s'.",
          parser->token->text_len, parser->token->text);
        break;
    }
  }
  parser_consume(parser);
  arr_t* arr = NEW_DATA(arr_t);
  arr->expr_list = list;
  node->data = arr;
  return node;
}

node_t* parse_ref(parser_t* parser, type_t* type) {
  if (type && !type->is_pointer) {
    parser_error(parser, "Type is not a pointer!");
    return NULL;
  }
  parser_consume(parser);
  node_t* expr = parse_simple_expr(parser);
  node_t* node = NEW_DATA(node_t);
  node->type = NODE_REF;
  node->lhs = expr;
  return node;
}

node_t* parse_struct_acc(parser_t* parser, type_t* type);

node_t* parse_id(parser_t* parser) {
  token_t* name = parser_expect(parser, TOK_ID);
  char* pname = parse_str(name);
  object_t* obj = parser_find_obj(parser, pname);
  node_t* node;
  if (parser_peek(parser)->type == TOK_DOT) {
    if (!obj->type->type->_struct) {
      parser_error(parser, "Trying to access member of non-struct type.");
      return NULL;
    }
    node = parse_struct_acc(parser, obj->type);
  } else {
    node = NEW_DATA(node_t);
    node->lhs = NULL; node->rhs = NULL;
    node->type = NODE_ID;
    node->tok = name;
    parser_consume(parser);
  }
  return node;
}

node_t* parse_struct_acc(parser_t* parser, type_t* type) {
  // Gotta end in =
  node_t* node = NEW_DATA(node_t);
  node->type = NODE_STRUCT_ACC;
  node->tok = parser->token;
  // Find object in members of type
  if (parser_consume(parser)->type == TOK_DOT) {
acc:
    if (!type->type->_struct) {
      parser_error(parser, "Trying to access member of non-struct type.");
      return NULL;
    }
    parser_consume(parser);
    list_t* list = type->type->members;
    bool found = false;
    type_t* ty;
    for (list_item_t* item = list->head->next; item != list->head; item = item->next) {
      var_t* var = (var_t*)item->data;
      if (!strncmp(parser->token->text, var->name->text, parser->token->text_len)) {
        ty = var->type;
        found = true;
      }
    }
    if (!found) {
      parser_error(parser, "No member '%.*s' found in structure %s.", parser->token->text_len, parser->token->text, type->type->name);
      return NULL;
    }
    node->lhs = parse_struct_acc(parser, ty);
    node->rhs = NULL;
    return node;
  } else if (parser->token->type == TOK_LSQBR) {
    parser_rewind(parser);
    node = parse_arr_idx(parser);
    if (parser_consume(parser)->type == TOK_DOT) {
      goto acc;
    }
  }
  return node;
}

// ID | array_index | @expr
node_t* parse_lvalue(parser_t* parser) {
  token_t* name = parser->token;
  node_t* node;
  if (name->type == TOK_AT) {
    parser_consume(parser);
    node_t* expr = parse_expr(parser, NULL);
    node_t* id = NULL;
    if (expr->lhs) {
      id = expr->lhs;
    } else {
      id = expr;
    }
    node = NEW_DATA(node_t);
    node->type = NODE_AT;
    node->lhs = expr;
    return node;
  }
  object_t* obj = parser_find_obj(parser, parse_str(name));
  if (obj->type->is_arr || obj->type->is_pointer) {
    // TODO: Check for -> if its pointer (maybe)
    if (parser_peek(parser)->type != TOK_LSQBR) {
      if (obj->type->is_pointer) goto els;
      parser_error(parser, "lvalue is an array (needs to be indexed).");
      return NULL;
    }
    node = parse_arr_idx(parser);
    parser_consume(parser);
    return node;
  } else {
els:
    node = parse_id(parser);
    return node;
  }
}

node_t* parse_var_decl(parser_t* parser, bool param, bool struc_member) {
  token_t* name = parser_consume(parser);
  parser_eat(parser, TOK_COLON);
  type_t* type = parser_get_type(parser);
  if (type->type->size == 0 && !type->is_pointer) {
    parser_error(parser, "You can't create a variable of type void.\n");
    return NULL;
  }
  token_t* next = parser_consume(parser);
  node_t* node = NEW_DATA(node_t);
  var_t* var = NEW_DATA(var_t);
  if (next->type == TOK_EQ) {
    if (param || struc_member) {
      parser_error(parser, "Cannot initialise variable in a parameter/member.");
      return NULL;
    }
    parser_consume(parser);
    node_t* lhs = parse_expr(parser, type);
    node->lhs = lhs;
    if (node->lhs->type != NODE_INT && node->lhs->type != NODE_ARRAY && node->lhs->type != NODE_STR && parser->scope->glob_scope) {
      parser_error(parser, "Trying to initialise a global variable with non-constant expression.");
      return NULL;
    }
    if (node->lhs->type == NODE_INT && type->is_pointer) {
      parser_error(parser, "Cannot assign an integer to pointer.");
      return NULL;
    }
  }
  if (param) {
    if (next->type != TOK_COMMA && next->type != TOK_RPAR && next->type != TOK_EQ) {
      parser_error(parser, "Unexpected token. Expected ',', ')' or '=' but got '%.*s'.", next->text_len, next->text);
    }
    if (parser->token->type != TOK_COMMA && parser->token->type != TOK_RPAR) {
      parser_error(parser, "Unexpected token. Expected ',' or ')' but got '%.*s'.", parser->token->text_len, parser->token->text);
    }
  } else {
    if (next->type != TOK_EQ && next->type != TOK_SEMI && !struc_member) {
      parser_error(parser, "Unexpected token. Expected '=' or ';' but got '%.*s'.", next->text_len, next->text);
    }
    if (parser->token->type != TOK_SEMI) {
      parser_error(parser, "Unexpected token. Expected ';' but got '%.*s'.", parser->token->text_len, parser->token->text);
    }
  }
  node->type = NODE_VAR_DEF;
  var->initialised = (next->type == TOK_EQ);
  var->name = name;
  var->type = type;
  var->glb = parser->scope->glob_scope;
  node->data = var;
  if (!struc_member) parser_new_obj(parser, type, name, parse_str(name), true, parser->scope->glob_scope, NULL);
  return node;
}

list_t* parser_param_list(parser_t* parser, bool* undef_params) {
  list_t* params = list_create();
  int count = 0;
  if (parser_peek(parser)->type == TOK_RPAR) {
    parser_eat(parser, TOK_RPAR);
    return params;
  }
  while (parser->token->type != TOK_RPAR) {
    if (parser_peek(parser)->type == TOK_3DOT) {
      *undef_params = true;
      parser_consume(parser);
      parser_eat(parser, TOK_RPAR);
      node_t* node = NEW_DATA(node_t);
      node->type = NODE_3DOT;
      node->tok = parser->token;
      list_add(params, node);
      return params;
      break;
    }
    parser_eat(parser, TOK_VAR); // Eat the var keyword
    list_add(params, parse_var_decl(parser, true, false));
    if (parser->token->type != TOK_COMMA && parser->token->type != TOK_RPAR) {
      parser_error(parser, "Unexpected token. Expected ',' or ')' but got '%.*s'.",
        parser->token->text_len, parser->token->text);
    }
  }
  return params;
}

list_t* parse_body(parser_t* parser) {
  list_t* body = list_create();
  while (parser->token->type != TOK_RBRAC) {
    list_add(body, parse_stmt(parser));
  }
  if (parser_peek(parser)->type != TOK_EOF) {
    parser_consume(parser);
  }
  return body;
}

node_t* parse_fn_decl(parser_t* parser) {
  token_t* name = parser_eat(parser, TOK_ID);
  parser_eat(parser, TOK_LPAR);
  fun_t* func = NEW_DATA(fun_t);
  scope_t* scope = parser_new_scope(parser);
  func->objs = list_create();
  parser->current_fn = func;
  parser->scope = scope;
  list_t* param_list = parser_param_list(parser, &func->undef_params);
  parser_eat(parser, TOK_COLON);
  type_t* type = parser_get_type(parser);
  token_t* open = parser_consume(parser);
  if (open->type != TOK_LBRAC && open->type != TOK_SEMI) {
    parser_error(parser, "Unexpected token. Expected '{' or ';' but got '%.*s'.", open->text_len, open->text);
  }
  node_t* node = NEW_DATA(node_t);
  func->initialised = true;
  parser_consume(parser);

  parser_new_obj(parser, type, name, parse_str(name), false, true, param_list)->undef_params = func->undef_params;

  list_t* body;
  if (open->type == TOK_LBRAC) {
    scope->type = type;
    body = parse_body(parser);
  }
  parser->scope = parser->scope->parent;
  if (open->type == TOK_SEMI) func->initialised = false;

  node->type = NODE_FN_DEF;
  func->name = name;
  func->params = param_list;
  func->body = body;
  func->_static = false;
  func->type = type;
  node->data = func;
  return node;
}

list_t* parse_expr_list(parser_t* parser, list_t* expected_params, bool undef_params) {
  list_t* expr_list = list_create();
  list_item_t* iterator = expected_params->head->next;
  type_t* type = NULL;
  while (parser->token->type != TOK_RPAR) {
    if (iterator == expected_params->head && !undef_params) {
      parser_error(parser, "Too many arguments to function call.");
      return NULL;
    }
    if (iterator != expected_params->head) {
      if (((node_t*)iterator->data)->type == NODE_3DOT) {
        iterator = iterator->next;
        type = NULL;
      } else {
        var_t* var = (var_t*)((node_t*)iterator->data)->data;
        type = var->type;
        iterator = iterator->next;
      }
    } else {
      type = NULL;
    }
    list_add(expr_list, parse_expr(parser, type));
    token_t* sep = parser->token;
    if (sep->type != TOK_COMMA && sep->type != TOK_RPAR) {
      parser_error(parser, "Unexpected token. Expected ',' or ')' but got '%.*s'.",
        sep->text_len, sep->text);
      return NULL;
    }
    if (sep->type == TOK_RPAR) break;
    parser_consume(parser);
  }
  if (expr_list->size < expected_params->size) {
    if (((node_t*)iterator->data)->type == NODE_3DOT) {
      return expr_list;
    }
    parser_error(parser, "Not enough arguments to function call.");
    return NULL;
  }
  return expr_list;
}

// fun(,x,y,z);
node_t* parse_fn_call(parser_t* parser, bool stmt) {
  token_t* name = parser->token;
  object_t* obj = parser_find_obj(parser, parse_str(name));
  if (!obj->func) {
    parser_error(parser, "'%s' is a variable, not a function.", obj->name);
    return NULL;
  }
  parser_eat(parser, TOK_LPAR);
  parser_consume(parser); // Skip to the first argument, or rpar
  list_t* arguments = parse_expr_list(parser, obj->params, obj->undef_params);
  node_t* node = NEW_DATA(node_t);
  funcall_t* fcall = NEW_DATA(funcall_t);
  node->type = NODE_FN_CALL;
  node->tok = name;
  fcall->name = name;
  fcall->arguments = arguments;
  node->data = fcall;
  if (stmt) parser_eat(parser, TOK_SEMI);
  return node;
}

node_t* parse_assign(parser_t* parser, bool exsemi) {
  node_t* lhs = parse_lvalue(parser);
  static int op_table[] = {
    [TOK_PLUS] = NODE_ASADD, [TOK_MINUS] = NODE_ASSUB, [TOK_STAR] = NODE_ASMUL,
    [TOK_DIV] = NODE_ASDIV, [TOK_MOD] = NODE_ASMOD, [TOK_LT] = NODE_ASSHL,
    [TOK_GT] = NODE_ASSHR, [TOK_PIPE] = NODE_ASOR, [TOK_AMPER] = NODE_ASAND,
    [TOK_HAT] = NODE_ASXOR,
  };
  token_t* operation = parser->token;
  int ast_type = NODE_ASSIGN;
  if (operation->type != TOK_EQ) {
    ast_type = op_table[operation->type];
    if (ast_type == NODE_ASSHL || ast_type == NODE_ASSHR) {
      parser_eat(parser, (ast_type == NODE_ASSHL ? TOK_LTEQ : TOK_GTEQ));
    } else {
      parser_eat(parser, TOK_EQ);
    }
  }
  parser_consume(parser);
  node_t* rhs = parse_expression(parser, NULL); // FIXME: Find object's type (can be an issue when referencing a ptr)
  node_t* node = NEW_DATA(node_t);
  node->type = ast_type;
  node->lhs = lhs;
  node->rhs = rhs;
  if (exsemi) parser_expect(parser, TOK_SEMI);
  else parser_rewind(parser);
  return node;
}

node_t* parse_ret(parser_t* parser) {
  node_t* node = NEW_DATA(node_t);
  node->type = NODE_RET;
  node->lhs = NULL;
  token_t* next = parser_consume(parser);
  if (next->type == TOK_SEMI) {
    parser_consume(parser);
    return node;
  }
  node->lhs = parse_expr(parser, parser->scope->type);
  parser_expect(parser, TOK_SEMI);
  return node;
}

node_t* parse_internal_condition(parser_t* parser) {
  node_t* lhs = parse_expression(parser, NULL);
  if ((parser->token->type < TOK_EQEQ || parser->token->type > TOK_LTEQ) && parser->token->type != TOK_EXCLEQ) {
    // if (expr), we treat that as if (expr >= 1)
    return lhs;
  }
  node_t* node = NEW_DATA(node_t);
  node->tok = parser->token;
  node->type = NODE_EQEQ + (parser->token->type - TOK_EQEQ);
  parser_consume(parser);
  node_t* rhs = parse_expression(parser, NULL);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

node_t* parse_condition(parser_t* parser) {
  node_t* lhs = parse_internal_condition(parser);
  node_t* node = lhs;
  if (parser->token->type == TOK_DBAMPER) {
    parser_consume(parser);
    node_t* rhs = parse_condition(parser);
    node = NEW_DATA(node_t);
    node->type = NODE_DBAND;
    node->lhs = lhs;
    node->rhs = rhs;
  } else if (parser->token->type == TOK_DBPIPE) {
    parser_consume(parser);
    node_t* rhs = parse_condition(parser);
    node = NEW_DATA(node_t);
    node->type = NODE_DBOR;
    node->lhs = lhs;
    node->rhs = rhs;
  }
  return node;
}

node_t* parse_compound_stmt(parser_t* parser) {
  parser_consume(parser);
  node_t* node = NEW_DATA(node_t);
  node->type = NODE_COMPOUND;
  scope_t* scope = parser_new_scope(parser);
  scope->type = parser->scope->type;
  parser->scope = scope;
  list_t* body = parse_body(parser);
  parser->scope = parser->scope->parent;
  node->data = body;
  return node;
}

node_t* parse_if(parser_t* parser) {
  parser_eat(parser, TOK_LPAR);
  parser_consume(parser); // Advance to the next token
  node_t* cond = parse_condition(parser);
  parser_expect(parser, TOK_RPAR);
  parser_consume(parser);

  if_stmt_t* stmt = NEW_DATA(if_stmt_t);
  memset(stmt, 0, sizeof(if_stmt_t));

  stmt->true_stmt = parse_stmt(parser);
  node_t* false_stmt = NULL;
  if (parser->token->type == TOK_ELSE) {
    parser_consume(parser);
    stmt->false_stmt = parse_stmt(parser);
  }

  node_t* node = NEW_DATA(node_t);
  node->type = NODE_IF;
  node->lhs = cond;
  node->data = stmt;
  return node;
}

// for (assign/var_decl; id => expr (; assignment)) {}
node_t* parse_for(parser_t* parser) {
  parser_eat(parser, TOK_LPAR);
  token_t* next = parser_consume(parser);
  node_t* primary_stmt;
  if (next->type == TOK_ID) {
    primary_stmt = parse_assign(parser, true);
  } else {
    parser_expect(parser, TOK_VAR);
    primary_stmt = parse_var_decl(parser, false, false);
  }
  parser_consume(parser);
  node_t* cond = parse_condition(parser);
  bool custom_step = false;
  node_t* step = NULL;
  parser_expect(parser, TOK_SEMI);
  parser_consume(parser);
  step = parse_assign(parser, false);
  custom_step = true;
  parser_consume(parser);
  parser_expect(parser, TOK_RPAR);
  parser_consume(parser);
  node_t* node = NEW_DATA(node_t);
  node->type = NODE_FOR;
  node->lhs = cond;
  for_stmt_t* stmt = NEW_DATA(for_stmt_t);
  stmt->custom_step = custom_step; stmt->primary_stmt = primary_stmt;
  stmt->step = step;
  stmt->body = parse_stmt(parser);
  node->data = stmt;
  return node;
}

node_t* parse_while(parser_t* parser) {
  parser_eat(parser, TOK_LPAR);
  parser_consume(parser); // Advance to the next token
  node_t* cond = parse_condition(parser);
  parser_expect(parser, TOK_RPAR);
  parser_consume(parser);
  node_t* stmt = parse_stmt(parser);
  bool initialised = true;
  node_t* node = NEW_DATA(node_t);
  while_stmt_t* wstmt = NEW_DATA(while_stmt_t);
  node->type = NODE_WHILE;
  node->lhs = cond;
  wstmt->initialised = initialised;
  wstmt->body = stmt;
  node->data = wstmt;
  return node;
}

void parse_struct_decl(parser_t* parser) {
  token_t* name = parser_eat(parser, TOK_ID);
  basetype_t* type = (basetype_t*)NEW_DATA(basetype_t);
  type_checker_add(parser->tychk, name, type);

  type->_struct = true;
  type->members = list_create();
  int size = 0;
  parser_eat(parser, TOK_LBRAC);
  parser_consume(parser);
  while (parser->token->type != TOK_RBRAC) {
    node_t* node = parse_var_decl(parser, false, true);
    var_t* var = ((var_t*)node->data);
    free(node);
    list_add(type->members, var);
    if (var->type->is_pointer) {
      size += 8;
    } else if (var->type->is_arr) {
      type_t* temp = var->type;
      while (temp->pointer) {
        temp = temp->pointer;
        size += var->type->type->size * temp->arr_size->value;
      }
    } else {
      size += var->type->type->size;
    }
    parser_consume(parser); // skip ;
  }
  parser_consume(parser); // eat }
  type->size = size;
}