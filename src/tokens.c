/*
 * tokens.c -- Printable names for token codes (used by --tokens).
 */
#include "tokens.h"

const char *token_name(int tok)
{
    switch (tok) {
    case YYEOF:      return "EOF";

    case KW_INT:     return "KW_INT";
    case KW_FLOAT:   return "KW_FLOAT";
    case KW_BOOL:    return "KW_BOOL";
    case KW_VOID:    return "KW_VOID";
    case KW_IF:      return "KW_IF";
    case KW_ELSE:    return "KW_ELSE";
    case KW_WHILE:   return "KW_WHILE";
    case KW_FOR:     return "KW_FOR";
    case KW_RETURN:  return "KW_RETURN";
    case KW_PRINT:   return "KW_PRINT";

    case IDENT:      return "IDENT";
    case INT_LIT:    return "INT_LIT";
    case FLOAT_LIT:  return "FLOAT_LIT";
    case BOOL_LIT:   return "BOOL_LIT";

    case OP_PLUS:    return "OP_PLUS";
    case OP_MINUS:   return "OP_MINUS";
    case OP_MUL:     return "OP_MUL";
    case OP_DIV:     return "OP_DIV";
    case OP_MOD:     return "OP_MOD";
    case OP_ASSIGN:  return "OP_ASSIGN";
    case OP_EQ:      return "OP_EQ";
    case OP_NE:      return "OP_NE";
    case OP_LT:      return "OP_LT";
    case OP_LE:      return "OP_LE";
    case OP_GT:      return "OP_GT";
    case OP_GE:      return "OP_GE";
    case OP_AND:     return "OP_AND";
    case OP_OR:      return "OP_OR";
    case OP_NOT:     return "OP_NOT";

    case LPAREN:     return "LPAREN";
    case RPAREN:     return "RPAREN";
    case LBRACE:     return "LBRACE";
    case RBRACE:     return "RBRACE";
    case SEMI:       return "SEMI";
    case COMMA:      return "COMMA";
    }
    return "UNKNOWN";
}
