/**
 * qemu-config.c - A C build tool for configuring QEMU via YAML
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>

#define MAX_LINE_LENGTH 1024
#define MAX_KEY_LENGTH 256
#define MAX_VALUE_LENGTH 1024
#define MAX_ARGS 100
#define MAX_ARG_LENGTH 1024
#define MAX_COMMAND_LENGTH 4096

typedef enum {
  YAML_SCALAR,
  YAML_LIST,
  YAML_DICT
} YamlNodeType;

typedef struct YamlNode {
  YamlNodeType type;
  char key[MAX_KEY_LENGTH];
  
  // For scalar type
  char value[MAX_VALUE_LENGTH];
  
  // For list or dict type
  struct YamlNode **children;
  int children_count;
  int children_capacity;
} YamlNode;

typedef struct {
  YamlNode *root;
} YamlDocument;

YamlNode* create_yaml_node(YamlNodeType type, const char *key);
void free_yaml_node(YamlNode *node);
void add_child_node(YamlNode *parent, YamlNode *child);
YamlDocument* parse_yaml_file(const char *filename);
void free_yaml_document(YamlDocument *doc);
int get_indent_level(const char *line);
void strip_trailing_whitespace(char *str);
void generate_qemu_command(YamlNode *node, char *command, size_t size);
void handle_qemu_parameter(const char *key, const char *value, char *command, size_t size);
void run_qemu_command(const char *command);
void print_yaml_node(YamlNode *node, int indent);

YamlNode* create_yaml_node(YamlNodeType type, const char *key) {
  YamlNode *node = (YamlNode*) malloc(sizeof(YamlNode));
  if (!node) {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  
  node->type = type;
  strncpy(node->key, key, MAX_KEY_LENGTH - 1);
  node->key[MAX_KEY_LENGTH - 1] = '\0';
  node->value[0] = '\0';
  node->children = NULL;
  node->children_count = 0;
  node->children_capacity = 0;
  
  return node;
}

void free_yaml_node(YamlNode *node) {
  if (!node) return;
  
  if (node->children) {
    for (int i = 0; i < node->children_count; i++) {
      free_yaml_node(node->children[i]);
    }
    free(node->children);
  }
  
  free(node);
}

void add_child_node(YamlNode *parent, YamlNode *child) {
  if (parent->children_count >= parent->children_capacity) {
    int new_capacity = parent->children_capacity == 0 ? 4 : parent->children_capacity * 2;
    YamlNode **new_children = (YamlNode**) realloc(parent->children, 
                          new_capacity * sizeof(YamlNode*));
    if (!new_children) {
      fprintf(stderr, "Memory allocation failed\n");
      exit(EXIT_FAILURE);
    }
    
    parent->children = new_children;
    parent->children_capacity = new_capacity;
  }
  
  parent->children[parent->children_count++] = child;
}

int get_indent_level(const char *line) {
  int indent = 0;
  while (*line == ' ') {
    indent++;
    line++;
  }
  return indent / 2;
}

void strip_trailing_whitespace(char *str) {
  int len = strlen(str);
  while (len > 0 && isspace((unsigned char)str[len - 1])) {
    str[--len] = '\0';
  }
}

YamlDocument* parse_yaml_file(const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "Could not open file: %s\n", filename);
    return NULL;
  }
  
  YamlDocument *doc = (YamlDocument*) malloc(sizeof(YamlDocument));
  if (!doc) {
    fprintf(stderr, "Memory allocation failed\n");
    fclose(file);
    return NULL;
  }
  
  doc->root = create_yaml_node(YAML_DICT, "root");
  
  char line[MAX_LINE_LENGTH];
  YamlNode *current_nodes[100]; // Stack for nested nodes
  int current_levels[100];    // Indentation levels
  int stack_size = 0;
  
  current_nodes[stack_size] = doc->root;
  current_levels[stack_size] = -1;
  stack_size++;
  
  while (fgets(line, sizeof(line), file)) {
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
    
    char *content = line;
    while (*content && isspace((unsigned char)*content)) content++;
    
    if (!*content || *content == '#') continue;
    
    strip_trailing_whitespace(line);
    
    int indent = get_indent_level(line);
    
    // Pop nodes off stack if we are at a lower indentation level
    while (stack_size > 1 && indent <= current_levels[stack_size - 1]) {
      stack_size--;
    }
    
    char *key_start = line + indent * 2;
    char *colon = strchr(key_start, ':');
    
    if (colon) {
      *colon = '\0';
      char *key = key_start;
      strip_trailing_whitespace(key);
      
      char *value_start = colon + 1;
      while (*value_start && isspace((unsigned char)*value_start)) value_start++;
      
      if (*value_start == '\0') {
        // This is a dictionary key without an inline value
        YamlNode *dict_node = create_yaml_node(YAML_DICT, key);
        add_child_node(current_nodes[stack_size - 1], dict_node);
        
        current_nodes[stack_size] = dict_node;
        current_levels[stack_size] = indent;
        stack_size++;
      } else {
        // This is a key-value pair
        YamlNode *scalar_node = create_yaml_node(YAML_SCALAR, key);
        strncpy(scalar_node->value, value_start, MAX_VALUE_LENGTH - 1);
        scalar_node->value[MAX_VALUE_LENGTH - 1] = '\0';
        add_child_node(current_nodes[stack_size - 1], scalar_node);
      }
    } else if (line[indent * 2] == '-') {
      // This is a list item 
      // skip the '-'
      char *value_start = line + indent * 2 + 1;
      
      while (*value_start && isspace((unsigned char)*value_start)) value_start++;
      
      // see if the parent is already a list or needs to be converted
      YamlNode *parent = current_nodes[stack_size - 1];
      if (parent->type != YAML_LIST && parent->children_count == 0) {
        parent->type = YAML_LIST;
      }
      
      // create a scalar node for the list item
      YamlNode *list_item = create_yaml_node(YAML_SCALAR, "");
      strncpy(list_item->value, value_start, MAX_VALUE_LENGTH - 1);
      list_item->value[MAX_VALUE_LENGTH - 1] = '\0';
      add_child_node(parent, list_item);
    }
  }
  
  fclose(file);
  return doc;
}

void free_yaml_document(YamlDocument *doc) {
  if (doc) {
    free_yaml_node(doc->root);
    free(doc);
  }
}

void print_yaml_node(YamlNode *node, int indent) {
  for (int i = 0; i < indent; i++) {
    printf("  ");
  }
  
  if (node->type == YAML_SCALAR) {
    printf("%s: %s\n", node->key, node->value);
  } else {
    printf("%s:\n", node->key);
    for (int i = 0; i < node->children_count; i++) {
      print_yaml_node(node->children[i], indent + 1);
    }
  }
}

void handle_qemu_parameter(const char *key, const char *value, char *command, size_t size) {
  char arg[MAX_ARG_LENGTH];
  
  if (strcmp(key, "memory") == 0) {
    snprintf(arg, sizeof(arg), "-m %s", value);
  } else if (strcmp(key, "cpu") == 0) {
    snprintf(arg, sizeof(arg), "-cpu %s", value);
  } else if (strcmp(key, "smp") == 0) {
    snprintf(arg, sizeof(arg), "-smp %s", value);
  } else if (strcmp(key, "kernel") == 0) {
    snprintf(arg, sizeof(arg), "-kernel %s", value);
  } else if (strcmp(key, "initrd") == 0) {
    snprintf(arg, sizeof(arg), "-initrd %s", value);
  } else if (strcmp(key, "append") == 0) {
    snprintf(arg, sizeof(arg), "-append \"%s\"", value);
  } else if (strcmp(key, "hda") == 0) {
    snprintf(arg, sizeof(arg), "-hda %s", value);
  } else if (strcmp(key, "cdrom") == 0) {
    snprintf(arg, sizeof(arg), "-cdrom %s", value);
  } else if (strcmp(key, "boot") == 0) {
    snprintf(arg, sizeof(arg), "-boot %s", value);
  } else if (strcmp(key, "net") == 0) {
    snprintf(arg, sizeof(arg), "-net %s", value);
  } else if (strcmp(key, "device") == 0) {
    snprintf(arg, sizeof(arg), "-device %s", value);
  } else if (strcmp(key, "drive") == 0) {
    snprintf(arg, sizeof(arg), "-drive %s", value);
  } else if (strcmp(key, "display") == 0) {
    snprintf(arg, sizeof(arg), "-display %s", value);
  } else if (strcmp(key, "vga") == 0) {
    snprintf(arg, sizeof(arg), "-vga %s", value);
  } else if (strcmp(key, "usb") == 0 && (strcmp(value, "true") == 0 || strcmp(value, "on") == 0)) {
    snprintf(arg, sizeof(arg), "-usb");
  } else if (strcmp(key, "nographic") == 0 && (strcmp(value, "true") == 0 || strcmp(value, "on") == 0)) {
    snprintf(arg, sizeof(arg), "-nographic");
  } else if (strcmp(key, "serial") == 0) {
    snprintf(arg, sizeof(arg), "-serial %s", value);
  } else if (strcmp(key, "parallel") == 0) {
    snprintf(arg, sizeof(arg), "-parallel %s", value);
  } else if (strcmp(key, "monitor") == 0) {
    snprintf(arg, sizeof(arg), "-monitor %s", value);
  } else if (strcmp(key, "runas") == 0) {
    snprintf(arg, sizeof(arg), "-runas %s", value);
  } else if (strcmp(key, "sandbox") == 0) {
    snprintf(arg, sizeof(arg), "-sandbox %s", value);
  } else if (strcmp(key, "name") == 0) {
    snprintf(arg, sizeof(arg), "-name %s", value);
  } else if (strcmp(key, "uuid") == 0) {
    snprintf(arg, sizeof(arg), "-uuid %s", value);
  } else if (strcmp(key, "machine") == 0) {
    snprintf(arg, sizeof(arg), "-machine %s", value);
  } else {
    snprintf(arg, sizeof(arg), "-%s %s", key, value);
  }
  
  strncat(command, " ", size - strlen(command) - 1);
  strncat(command, arg, size - strlen(command) - 1);
}

void generate_qemu_command(YamlNode *node, char *command, size_t size) {
  if (!node) return;
  
  if (node->type == YAML_DICT) {
    for (int i = 0; i < node->children_count; i++) {
      YamlNode *child = node->children[i];
      if (strcmp(child->key, "qemu_binary") == 0) {
        strncpy(command, child->value, size - 1);
        command[size - 1] = '\0';
        break;
      }
    }
    
    if (command[0] == '\0') {
      // TODO: handle other arch commands
      strncpy(command, "qemu-system-x86_64", size - 1);
      command[size - 1] = '\0';
    }
    
    for (int i = 0; i < node->children_count; i++) {
      YamlNode *child = node->children[i];
      
      if (strcmp(child->key, "qemu_binary") == 0) continue;
      
      if (child->type == YAML_SCALAR) {
        handle_qemu_parameter(child->key, child->value, command, size);
      } else if (child->type == YAML_DICT) {
        char nested_key[MAX_KEY_LENGTH * 2];
        for (int j = 0; j < child->children_count; j++) {
          YamlNode *grandchild = child->children[j];
          if (grandchild->type == YAML_SCALAR) {
            snprintf(nested_key, sizeof(nested_key), "%s.%s", child->key, grandchild->key);
            handle_qemu_parameter(nested_key, grandchild->value, command, size);
          }
        }
      } else if (child->type == YAML_LIST) {
        for (int j = 0; j < child->children_count; j++) {
          YamlNode *list_item = child->children[j];
          if (list_item->type == YAML_SCALAR) {
            handle_qemu_parameter(child->key, list_item->value, command, size);
          }
        }
      }
    }
  }
}

void run_qemu_command(const char *command) {
  printf("Executing: %s\n", command);
  
  int status = system(command);
  
  if (status == -1) {
    fprintf(stderr, "Failed to execute command\n");
  } else if (WIFEXITED(status)) {
    printf("Command exited with status %d\n", WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    printf("Command killed by signal %d\n", WTERMSIG(status));
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <yaml_config_file> [--dry-run]\n", argv[0]);
    return EXIT_FAILURE;
  }
  
  const char *config_file = argv[1];
  bool dry_run = false;
  
  if (argc > 2 && strcmp(argv[2], "--dry-run") == 0) {
    dry_run = true;
  }
  
  YamlDocument *doc = parse_yaml_file(config_file);
  if (!doc) {
    fprintf(stderr, "Failed to parse YAML file\n");
    return EXIT_FAILURE;
  }
  
  char command[MAX_COMMAND_LENGTH] = "";
  generate_qemu_command(doc->root, command, sizeof(command));
  
  printf("Generated QEMU command:\n%s\n", command);
  
  if (!dry_run) {
    run_qemu_command(command);
  }
  
  free_yaml_document(doc);
  
  return EXIT_SUCCESS;
}
