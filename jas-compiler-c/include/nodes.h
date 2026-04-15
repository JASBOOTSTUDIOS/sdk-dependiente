/* Nodos AST - Paridad con nodes.py */

#ifndef NODES_H
#define NODES_H

#include <stddef.h>
#include <stdint.h>

typedef struct ASTNode ASTNode;

typedef enum {
    NODE_PROGRAM,
    NODE_BLOCK,
    NODE_FUNCTION,
    NODE_PRINT,
    NODE_ASSIGNMENT,
    NODE_VAR_DECL,
    NODE_BINARY_OP,
    NODE_LITERAL,
    NODE_IDENTIFIER,
    NODE_CALL,
    NODE_WHILE,
    NODE_FOREACH,
    NODE_DO_WHILE,
    NODE_IF,
    NODE_RETURN,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_INPUT,
    NODE_RECORDAR,
    NODE_RESPONDER,
    NODE_APRENDER,
    NODE_BUSCAR_PESO,
    NODE_STRUCT_DEF,
    NODE_MEMBER_ACCESS,
    NODE_ASOCIAR,
    NODE_ACTIVAR_MODULO,
    NODE_BIBLIOTECA,
    NODE_DEFINE_CONCEPTO,
    NODE_EXTRAER_TEXTO,
    NODE_CONTIENE_TEXTO,
    NODE_TERMINA_CON,
    NODE_ULTIMA_PALABRA,
    NODE_COPIAR_TEXTO,
    NODE_CREAR_MEMORIA,
    NODE_CERRAR_MEMORIA,
    NODE_END_DO_WHILE,
    NODE_LIST_LITERAL,
    NODE_MAP_LITERAL,
    NODE_JSON_LITERAL, /* Misma forma que MapLiteralNode: claves texto, valores solo literales/anidados */
    NODE_INDEX_ACCESS,
    NODE_INDEX_ASSIGNMENT,
    NODE_TERNARY,
    NODE_UNARY_OP,
    NODE_POSTFIX_UPDATE,
    NODE_SELECT,
    NODE_TRY,
    NODE_THROW,
    NODE_LAMBDA_DECL,
    NODE_EXPORT_DIRECTIVE
} NodeType;

struct ASTNode {
    NodeType type;
    int line;
    int col;
};

/* 2.1 Nodos base */
typedef struct {
    ASTNode base;
    ASTNode **functions;
    ASTNode *main_block;
    ASTNode **globals;
    size_t n_funcs;
    size_t n_globals;
} ProgramNode;

typedef struct {
    ASTNode base;
    ASTNode **statements;
    size_t n;
} BlockNode;

typedef struct {
    ASTNode base;
    char *name;
    char *return_type; /* texto, entero, flotante, tarea, etc. */
    char *return_task_elem; /* si retorna tarea<T>: T (texto, entero, ...); NULL si no aplica */
    ASTNode **params;  /* VarDeclNode */
    size_t n_params;
    ASTNode *body;     /* BlockNode */
    int is_exported;   /* 1 si lleva `enviar` (visible desde otros archivos con usar filtrado) */
    int is_async;      /* 1 si se declaro con `asincrono` (planificacion real: trabajo futuro en VM) */
} FunctionNode;

/* 2.2 Declaraciones */
typedef struct {
    ASTNode base;
    char *type_name;
    char *name;
    ASTNode *value;
    int is_const;  /* 1 si es constante (inmutable) */
    int is_exported; /* 1 si declaracion global lleva `enviar` */
    char *list_element_type; /* para `lista<T>`: T (entero, flotante, ...); NULL si no aplica */
} VarDeclNode;

typedef struct {
    ASTNode base;
    char *name;
    char *extends_name; /* NULL si no hay `extiende Base`; solo clases/registros con herencia */
    char **field_types;
    char **field_names;
    size_t n_fields;
    ASTNode **methods;        /* FunctionNode */
    size_t n_methods;
    ASTNode **nested_structs;  /* StructDefNode */
    size_t n_nested_structs;
    int *field_visibilities;  /* 0=publico, 1=privado */
    int *method_visibilities; /* 0=publico, 1=privado */
    int is_exported;
} StructDefNode;

/* 2.3 Expresiones */
typedef struct {
    ASTNode base;
    ASTNode *left;
    char *operator;
    ASTNode *right;
    int line;
    int col;
} BinaryOpNode;

typedef struct {
    ASTNode base;
    char *type_name;  /* entero, texto, flotante, concepto */
    union {
        int64_t i;
        double f;
        char *str;
    } value;
    int is_float;
} LiteralNode;

typedef struct {
    ASTNode base;
    char *name;
    int line;
    int col;
} IdentifierNode;

/* 2.4 Llamadas */
typedef struct {
    ASTNode base;
    char *name;           /* llamada nombre(...) — NULL si solo hay callee */
    ASTNode *callee;      /* llamada indirecta expr(...); si name != NULL, callee es NULL */
    ASTNode **args;
    size_t n_args;
} CallNode;

/* 2.5 Control de flujo */
typedef struct {
    ASTNode base;
    ASTNode *condition;
    ASTNode *body;
} WhileNode;

/* para_cada T id sobre expr hacer ... fin_para_cada */
typedef struct {
    ASTNode base;
    char *iter_type;
    char *iter_name;
    ASTNode *collection;
    ASTNode *body;
} ForEachNode;

typedef struct {
    ASTNode base;
    ASTNode *condition;
    ASTNode *body;
} DoWhileNode;

typedef struct {
    ASTNode base;
    ASTNode *condition;
} EndDoWhileNode;

typedef struct {
    ASTNode base;
    ASTNode *condition;
    ASTNode *body;
    ASTNode *else_body;
} IfNode;

typedef struct {
    ASTNode base;
    ASTNode *expression;
} ReturnNode;

typedef struct { ASTNode base; } BreakNode;
typedef struct { ASTNode base; } ContinueNode;

/* 2.6 I/O */
typedef struct {
    ASTNode base;
    ASTNode *expression;
} PrintNode;

typedef struct {
    ASTNode base;
    char *variable;
    int immediate; /* 0=ingresar_texto (bloqueante), 1=ingreso_inmediato (tecla/input inmediato) */
} InputNode;

typedef struct {
    ASTNode **values;   /* expresiones del caso (v1, v2, ...) */
    size_t n_values;
    ASTNode *body;      /* BlockNode */
    int is_range;       /* 1 if it's a range case (a..b) */
    ASTNode *range_end; /* The end value for a range case (range_start is in values[0]) */
} SelectCase;

typedef struct {
    ASTNode base;
    ASTNode *selector;
    SelectCase *cases;
    size_t n_cases;
    ASTNode *default_body; /* BlockNode opcional */
} SelectNode;

typedef struct {
    ASTNode base;
    ASTNode *try_body;      /* BlockNode */
    char *catch_var;        /* opcional */
    ASTNode *catch_body;    /* BlockNode opcional */
    ASTNode *final_body;    /* BlockNode opcional */
} TryNode;

typedef struct {
    ASTNode base;
    ASTNode *expression;    /* valor/mensaje de error */
} ThrowNode;

/* 2.7 Memoria neuronal */
typedef struct {
    ASTNode base;
    ASTNode *key;
    ASTNode *value;
} RecordarNode;

typedef struct {
    ASTNode base;
    ASTNode *message;
} ResponderNode;

typedef struct {
    ASTNode base;
    ASTNode *concept;
    ASTNode *weight;
} AprenderNode;

typedef struct {
    ASTNode base;
    ASTNode *concept;
} BuscarPesoNode;

typedef struct {
    ASTNode base;
    ASTNode *concept1;
    ASTNode *concept2;
    ASTNode *weight;
} AsociarNode;

/* 2.8 Módulos / importacion (usar) */
#define USAR_IMPORT_LEGACY 0 /* no emitido por el parser; reservado internamente */
#define USAR_IMPORT_TODO   1 /* usar todo/todas de "ruta.jasb" — solo simbolos con `enviar` */
#define USAR_IMPORT_NAMES  2 /* usar { a, b } de "ruta.jasb" — valida nombres exportados; fusion de modulo = todo `enviar` */

typedef struct {
    ASTNode base;
    ASTNode *module_path;  /* LiteralNode (ruta texto) */
    int import_kind;       /* USAR_IMPORT_* */
    char **import_names;   /* solo USAR_IMPORT_NAMES; cada cadena es propiedad del nodo */
    size_t n_import_names;
} ActivarModuloNode;

/* 2.9 Memoria */
typedef struct {
    ASTNode base;
    ASTNode *filename;
    ASTNode *nodes_capacity;
    ASTNode *connections_capacity;
} CrearMemoriaNode;

typedef struct { ASTNode base; } CerrarMemoriaNode;

/* 2.10 Texto/NLP */
typedef struct {
    ASTNode base;
    ASTNode *source;
    ASTNode *pattern;
    char *target;
    int mode;  /* 0=antes, 1=despues */
} ExtraerTextoNode;

typedef struct {
    ASTNode base;
    ASTNode *source;
    ASTNode *pattern;
} ContieneTextoNode;

typedef struct {
    ASTNode base;
    ASTNode *source;
    ASTNode *suffix;  /* o pattern para termina_con */
} TerminaConNode;

typedef struct {
    ASTNode base;
    ASTNode *source;
    char *target;
} UltimaPalabraNode;

typedef struct {
    ASTNode base;
    ASTNode *source;
    char *target;
} CopiarTextoNode;

/* Asignación (x = expr) */
typedef struct {
    ASTNode base;
    ASTNode *target;
    ASTNode *expression;
} AssignmentNode;

/* 2.11 Colecciones */
typedef struct {
    ASTNode base;
    ASTNode **elements;
    size_t n;
} ListLiteralNode;

typedef struct {
    ASTNode base;
    ASTNode **keys;
    ASTNode **values;
    size_t n;
} MapLiteralNode;

typedef struct {
    ASTNode base;
    ASTNode *target;
    ASTNode *index;
} IndexAccessNode;

typedef struct {
    ASTNode base;
    ASTNode *target;
    ASTNode *index;
    ASTNode *expression;
} IndexAssignmentNode;

/* 2.12 Miembros */
typedef struct {
    ASTNode base;
    ASTNode *target;
    char *member;
} MemberAccessNode;

/* 2.13 Ternario */
typedef struct {
    ASTNode base;
    ASTNode *condition;
    ASTNode *true_expr;
    ASTNode *false_expr;
} TernaryNode;

/* Unary */
typedef struct {
    ASTNode base;
    char *operator;
    ASTNode *expression;
} UnaryOpNode;

/* i++, i-- (valor de la expresion = valor anterior; efecto: actualiza variable) */
typedef struct {
    ASTNode base;
    ASTNode *target;
    int delta;
} PostfixUpdateNode;

/* 2.15 Conceptos */
typedef struct {
    ASTNode base;
    ASTNode *concepto;
    ASTNode *descripcion;
} DefineConceptoNode;

/* 2.14 Biblioteca */
typedef struct {
    ASTNode base;
    ASTNode *library_path;  /* LiteralNode con ruta */
} BibliotecaNode;

/* Funciones */
void ast_free(ASTNode *node);
ASTNode *ast_dup(const ASTNode *node);

typedef struct {
    ASTNode base;
    char **params;
    char **types;
    size_t n_params;
    ASTNode *body;
} LambdaDeclNode;

typedef struct {
    ASTNode base;
    char **names;
    size_t n_names;
} ExportDirectiveNode;

#endif /* NODES_H */
