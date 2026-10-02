/* qmake - small C89 build driver; initial host prototype. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#define MAX_RULES 96
#define MAX_DEPS 12
#define MAX_NAME 128
#define MAX_COMMAND 1024
#define MAX_LINE 1024
#define MAX_VARS 32
#define MAX_VAR_NAME 64
#define MAX_SUBDIRS 16
#define MAX_DEPTH 16
#define MAX_CONFIGS 16

typedef struct Variable Variable;
struct Variable { char name[MAX_VAR_NAME]; char value[MAX_COMMAND]; int command_line; };

typedef struct Rule Rule;
typedef struct Recipe Recipe;
struct Recipe {
    Recipe *next;
    char command[1];
};

struct Rule {
    char target[MAX_NAME];
    char deps[MAX_DEPS][MAX_NAME];
    Recipe *recipes, *last_recipe;
    int dep_count, command_count, state, global_rule, explicit_global_rule;
};

static Rule rules[MAX_RULES];
static int rule_count, verbosity, dry_run, errors;
static Variable variables[MAX_VARS];
static int variable_count;
static Variable overrides[MAX_VARS];
static int override_count;
static char config_dir_override[MAX_COMMAND];
static char project_file[MAX_COMMAND] = "q9makefile";
static int config_dir_is_set;
static char active_section[MAX_NAME] = "global";
static char selected_section[MAX_NAME];
static char configuration_names[MAX_CONFIGS][MAX_NAME];
static int configuration_count;
static int section_matched_target;
static char reported_toolchain_profiles[MAX_CONFIGS][MAX_COMMAND];
static int reported_toolchain_profile_count;
static FILE *failed_command_output;
static int failed_command_status;

static int implicit_object(const char *target);
static int run_command(const char *raw_command, const char *target);
static int load_project_description(void);
static void reset_description(void);
static int valid_path_component(const char *component);
static char *trim(char *s);
static int should_report_target(const char *target);
static int is_cross_profile_host_helper(const char *target);
static void report_target_start(const char *target);
static void report_target_finish(int success, int up_to_date, double elapsed);
static void report_recipe_failure(const char *target);
static void replay_output(FILE *input, FILE *output);

static void usage(const char *program)
{
    printf("Usage: %s [-n] [-v|-vv] [-f makefile] [-C profile-dir] [-P configuration] [-DNAME=value] [target]\n", program);
    printf("Build targets from ./q9makefile, or from the file selected with -f.\n");
    printf("With no target, qmake runs the global default target, or each profile's first target.\n");
    printf("  -? -h --help       show this command-line help\n");
    printf("  -n --dry-run       print commands without running them\n");
    printf("  -v / -vv           show target status / commands and detailed checks\n");
    printf("  -f FILE             use FILE instead of ./q9makefile\n");
    printf("  -C DIR              read DIR/qmake.conf for toolchain settings\n");
    printf("  -P NAME             run only configuration NAME; otherwise matching profiles run\n");
    printf("  -DNAME=value        override a make variable\n");
    printf("  --list-configs      list configuration sections in the project file\n");
}

static int section_matches(const char *name)
{
    return strcmp(name, "global") == 0 || strcmp(name, active_section) == 0;
}

static int add_configuration(const char *name)
{
    int i;
    if (!valid_path_component(name) || strlen(name) >= MAX_NAME) return 0;
    for (i = 0; i < configuration_count; ++i)
        if (strcmp(configuration_names[i], name) == 0) return 1;
    if (configuration_count >= MAX_CONFIGS) return 0;
    strcpy(configuration_names[configuration_count++], name);
    return 1;
}

static int discover_configurations(void)
{
    FILE *file;
    char line[MAX_LINE];
    configuration_count = 0;
    if (!add_configuration("global")) return 0;
    file = fopen(project_file, "r");
    if (file == (FILE *)0) {
        fprintf(stderr, "qmake: cannot open '%s'\n", project_file);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != (char *)0) {
        char *p = trim(line);
        size_t length = strlen(p);
        if (length >= 3 && p[0] == '[' && p[length - 1] == ']') {
            p[length - 1] = '\0';
            if (!add_configuration(p + 1)) {
                fprintf(stderr, "qmake: invalid or too many configuration sections\n");
                fclose(file);
                return 0;
            }
        }
    }
    fclose(file);
    return 1;
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = '\0';
    return s;
}

static const char *host_platform(void)
{
#if defined(_Q9OS) || defined(_OSK)
    return "q9";
#elif defined(_WIN32) || defined(_WIN64)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

static Rule *find_rule(const char *target)
{
    int i;
    for (i = 0; i < rule_count; ++i)
        if (strcmp(rules[i].target, target) == 0) return &rules[i];
    return (Rule *)0;
}

static int valid_name(const char *name)
{
    const char *p = name;
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_'))
        return 0;
    while (*p != '\0') {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_')) return 0;
        ++p;
    }
    return 1;
}

static int valid_path_component(const char *component)
{
    const char *p = component;
    if (*p == '\0' || strcmp(p, ".") == 0 || strcmp(p, "..") == 0) return 0;
    while (*p != '\0') {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return 0;
        ++p;
    }
    return 1;
}

static Variable *find_variable(const char *name)
{
    int i;
    for (i = 0; i < variable_count; ++i)
        if (strcmp(variables[i].name, name) == 0) return &variables[i];
    return (Variable *)0;
}

static Variable *find_override(const char *name)
{
    int i;
    for (i = 0; i < override_count; ++i)
        if (strcmp(overrides[i].name, name) == 0) return &overrides[i];
    return (Variable *)0;
}

static int set_override(const char *name, const char *value)
{
    Variable *var = find_override(name);
    if (!valid_name(name) || strlen(name) >= MAX_VAR_NAME || strlen(value) >= MAX_COMMAND)
        return 0;
    if (var == (Variable *)0) {
        if (override_count >= MAX_VARS) return 0;
        var = &overrides[override_count++];
        memset(var, 0, sizeof(*var));
        strcpy(var->name, name);
    }
    strcpy(var->value, value);
    var->command_line = 1;
    return 1;
}

static int set_variable(const char *name, const char *value, int command_line)
{
    Variable *var = find_variable(name);
    if (!valid_name(name) || strlen(name) >= MAX_VAR_NAME || strlen(value) >= MAX_COMMAND)
        return 0;
    if (var == (Variable *)0) {
        if (variable_count >= MAX_VARS) return 0;
        var = &variables[variable_count++];
        memset(var, 0, sizeof(*var));
        strcpy(var->name, name);
    }
    if (command_line || !var->command_line) {
        strcpy(var->value, value);
        var->command_line = command_line;
    }
    return 1;
}

static const char *variable_value(const char *name)
{
    Variable *var = find_variable(name);
    Variable *override = find_override(name);
    const char *environment = getenv(name);
    if (override != (Variable *)0) return override->value;
    if (environment != (const char *)0) return environment;
    return var == (Variable *)0 ? "" : var->value;
}

static int append_text(char *out, size_t capacity, size_t *used, const char *text)
{
    size_t length = strlen(text);
    if (*used + length >= capacity) return 0;
    memcpy(out + *used, text, length);
    *used += length;
    out[*used] = '\0';
    return 1;
}

static int expand_implicit_fields(const char *input, const char *source,
                                  const char *target, char *out, size_t capacity)
{
    size_t used = 0;
    const char *p = input;
    out[0] = '\0';
    while (*p != '\0') {
        const char *value = (const char *)0;
        if (strncmp(p, "@SOURCE@", 8) == 0) {
            value = source;
            p += 8;
        } else if (strncmp(p, "@TARGET@", 8) == 0) {
            value = target;
            p += 8;
        } else {
            char one[2];
            one[0] = *p++;
            one[1] = '\0';
            if (!append_text(out, capacity, &used, one)) return 0;
        }
        if (value != (const char *)0 &&
            !append_text(out, capacity, &used, value)) return 0;
    }
    return 1;
}

static int expand_text(const char *input, char *out, size_t capacity, int depth)
{
    size_t used = 0;
    const char *p = input;
    if (depth > 8 || capacity == 0) return 0;
    out[0] = '\0';
    while (*p != '\0') {
        if (p[0] == '$' && p[1] == '(') {
            const char *end = strchr(p + 2, ')');
            if (end != (const char *)0) {
                char name[MAX_VAR_NAME];
                char expanded[MAX_COMMAND];
                size_t length = (size_t)(end - (p + 2));
                if (length == 0 || length >= sizeof(name)) return 0;
                memcpy(name, p + 2, length);
                name[length] = '\0';
                if (!valid_name(name) ||
                    !expand_text(variable_value(name), expanded, sizeof(expanded), depth + 1) ||
                    !append_text(out, capacity, &used, expanded)) return 0;
                p = end + 1;
                continue;
            }
        }
        if (used + 1 >= capacity) return 0;
        out[used++] = *p++;
        out[used] = '\0';
    }
    return 1;
}

static Rule *find_expanded_rule(const char *target, char *resolved_target,
                                size_t capacity)
{
    int i;
    for (i = 0; i < rule_count; ++i) {
        char candidate[MAX_COMMAND];
        if (!expand_text(rules[i].target, candidate, sizeof(candidate), 0))
            continue;
        if (strcmp(candidate, target) == 0) {
            if (strlen(candidate) >= capacity) return (Rule *)0;
            strcpy(resolved_target, candidate);
            return &rules[i];
        }
    }
    return (Rule *)0;
}

static int add_dependency(Rule *rule, const char *name)
{
    if (rule->dep_count >= MAX_DEPS || strlen(name) >= MAX_NAME) return 0;
    strcpy(rule->deps[rule->dep_count++], name);
    return 1;
}

static int add_recipe(Rule *rule, const char *command)
{
    size_t length = strlen(command);
    Recipe *recipe;
    if (length >= MAX_COMMAND) return 0;
    recipe = (Recipe *)malloc(sizeof(*recipe) + length);
    if (recipe == (Recipe *)0) return 0;
    recipe->next = (Recipe *)0;
    strcpy(recipe->command, command);
    if (rule->last_recipe == (Recipe *)0)
        rule->recipes = recipe;
    else
        rule->last_recipe->next = recipe;
    rule->last_recipe = recipe;
    ++rule->command_count;
    return 1;
}

static int load_description(const char *path)
{
    FILE *file;
    char line[MAX_LINE];
    char file_section[MAX_NAME] = "global";
    Rule *current = (Rule *)0;
    int line_number = 0, explicit_global_section = 0;
    file = fopen(path, "r");
    if (file == (FILE *)0) {
        fprintf(stderr, "qmake: cannot open %s\n", path);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != (char *)0) {
        char *p, *colon, *equal;
        ++line_number;
        p = trim(line);
        if (*p == '[') {
            size_t length = strlen(p);
            if (length < 3 || p[length - 1] != ']') {
                fprintf(stderr, "qmake: malformed configuration section at %s:%d\n",
                        path, line_number);
                ++errors;
                current = (Rule *)0;
                continue;
            }
            p[length - 1] = '\0';
            if (!valid_path_component(p + 1)) {
                fprintf(stderr, "qmake: invalid configuration name at %s:%d\n",
                        path, line_number);
                ++errors;
                current = (Rule *)0;
                continue;
            }
            strcpy(file_section, p + 1);
            explicit_global_section = strcmp(file_section, "global") == 0;
            current = (Rule *)0;
            continue;
        }
        if (!section_matches(file_section)) {
            current = (Rule *)0;
            continue;
        }
        if (line[0] == '\t' || line[0] == ' ') {
            if (*p == '\0' || *p == '#') continue;
            if (current == (Rule *)0 || !add_recipe(current, p)) {
                fprintf(stderr, "qmake: invalid recipe at %s:%d\n", path, line_number);
                ++errors;
                continue;
            }
            continue;
        }
        if (*p == '\0' || *p == '#') continue;
        equal = strchr(p, '=');
        colon = strchr(p, ':');
        if (equal != (char *)0 && (colon == (char *)0 || equal < colon)) {
            char *name_end = equal;
            char *value;
            char name[MAX_VAR_NAME];
            while (name_end > p && (name_end[-1] == ' ' || name_end[-1] == '\t'))
                --name_end;
            if ((size_t)(name_end - p) >= sizeof(name)) {
                fprintf(stderr, "qmake: variable name too long at %s:%d\n", path, line_number);
                ++errors;
                continue;
            }
            memcpy(name, p, (size_t)(name_end - p));
            name[name_end - p] = '\0';
            value = trim(equal + 1);
            if (!set_variable(name, value, 0)) {
                fprintf(stderr, "qmake: invalid/too many variables at %s:%d\n",
                        path, line_number);
                ++errors;
            }
            current = (Rule *)0;
            continue;
        }
        if (colon == (char *)0) {
            fprintf(stderr, "qmake: expected target ':' prerequisites at %s:%d\n",
                    path, line_number);
            ++errors;
            current = (Rule *)0;
            continue;
        }
        *colon++ = '\0';
        p = trim(p);
        colon = trim(colon);
        if (*p == '\0' || strlen(p) >= MAX_NAME || rule_count >= MAX_RULES ||
            find_rule(p) != (Rule *)0) {
            fprintf(stderr, "qmake: invalid or duplicate target at %s:%d\n",
                    path, line_number);
            ++errors;
            current = (Rule *)0;
            continue;
        }
        current = &rules[rule_count++];
        memset(current, 0, sizeof(*current));
        strcpy(current->target, p);
        current->global_rule = strcmp(file_section, "global") == 0;
        current->explicit_global_rule = current->global_rule && explicit_global_section;
        while (*colon != '\0') {
            char *end;
            while (*colon == ' ' || *colon == '\t') ++colon;
            if (*colon == '\0') break;
            end = colon;
            while (*end != '\0' && *end != ' ' && *end != '\t' &&
                   *end != '\r' && *end != '\n') ++end;
            if (*end != '\0') *end++ = '\0';
            if (!add_dependency(current, colon)) {
                fprintf(stderr, "qmake: too many/long prerequisites at %s:%d\n",
                        path, line_number);
                ++errors;
                break;
            }
            colon = end;
        }
    }
    fclose(file);
    return errors == 0 && (rule_count > 0 || variable_count > 0);
}

/* Load the selected toolchain first, then let the project override its defaults. */
static int load_project_description(void)
{
    char toolchain_file[MAX_COMMAND];
    char config_dir[MAX_COMMAND];
    char expanded[MAX_COMMAND];
    const char *selection;
    const char *target_arch;
    long ignored_time;
    int explicit_profile = 0, profile_index, profile_reported = 0;

    reset_description();
    if (!load_description(project_file)) return 0;
    selection = variable_value("TOOLCHAIN_FILE");
    toolchain_file[0] = '\0';
    if (config_dir_is_set) {
        if (strlen(config_dir_override) + sizeof("/qmake.conf") >
            sizeof(toolchain_file)) {
            fprintf(stderr, "qmake: config directory path too long\n");
            return 0;
        }
        sprintf(toolchain_file, "%s/qmake.conf", config_dir_override);
    } else if (qmake_default_host_config_dir(config_dir, sizeof(config_dir)) &&
               strlen(config_dir) + sizeof("/qmake.conf") <= sizeof(toolchain_file)) {
        sprintf(toolchain_file, "%s/qmake.conf", config_dir);
        if (!qmake_file_info(toolchain_file, &ignored_time))
            toolchain_file[0] = '\0';
    }
    if (toolchain_file[0] == '\0' && selection[0] != '\0') {
        explicit_profile = 1;
        if (!expand_text(selection, expanded, sizeof(expanded), 0) ||
            expanded[0] == '\0') {
            fprintf(stderr, "qmake: invalid TOOLCHAIN_FILE value\n");
            return 0;
        }
        if (strlen(expanded) >= sizeof(toolchain_file)) {
            fprintf(stderr, "qmake: toolchain profile path too long\n");
            return 0;
        }
        strcpy(toolchain_file, expanded);
    }
    if (toolchain_file[0] == '\0' && strcmp(active_section, "global") != 0) {
        target_arch = variable_value("TARGET_ARCH");
        if (target_arch[0] != '\0' && valid_path_component(target_arch) &&
            qmake_default_config_dir(target_arch, config_dir, sizeof(config_dir)) &&
            strlen(config_dir) + sizeof("/qmake.conf") <= sizeof(toolchain_file)) {
            sprintf(toolchain_file, "%s/qmake.conf", config_dir);
        } else {
            toolchain_file[0] = '\0';
        }
        if (toolchain_file[0] == '\0' ||
            !qmake_file_info(toolchain_file, &ignored_time)) {
            if (selection[0] == '\0')
                strcpy(toolchain_file, "toolchains/qmake.conf");
        }
    } else if (toolchain_file[0] == '\0') {
        target_arch = variable_value("TARGET_ARCH");
        if (target_arch[0] == '\0') return 1;
        if (!valid_path_component(target_arch)) {
            fprintf(stderr, "qmake: invalid TARGET_ARCH '%s'\n", target_arch);
            return 0;
        }
        if (!qmake_default_config_dir(target_arch, config_dir,
                                     sizeof(config_dir))) return 1;
        if (strlen(config_dir) + sizeof("/qmake.conf") >
            sizeof(toolchain_file)) {
            fprintf(stderr, "qmake: default config path too long\n");
            return 0;
        }
        sprintf(toolchain_file, "%s/qmake.conf", config_dir);
    }

    if (!explicit_profile && !qmake_file_info(toolchain_file, &ignored_time))
        return 1;

    reset_description();
    if (verbosity > 0) {
        for (profile_index = 0; profile_index < reported_toolchain_profile_count;
             ++profile_index) {
            if (strcmp(reported_toolchain_profiles[profile_index], toolchain_file) == 0) {
                profile_reported = 1;
                break;
            }
        }
        if (!profile_reported) {
            printf("qmake: loading toolchain profile '%s'\n", toolchain_file);
            if (reported_toolchain_profile_count < MAX_CONFIGS) {
                strcpy(reported_toolchain_profiles[reported_toolchain_profile_count++],
                       toolchain_file);
            }
        }
    }
    if (!load_description(toolchain_file)) return 0;
    if (!load_description(project_file)) return 0;
    return 1;
}

static int build(const char *target)
{
    char resolved_target[MAX_COMMAND];
    Rule *rule = find_rule(target);
    Recipe *recipe;
    long newest = -1L, target_time;
    int exists, i, needs_build = 0, implicit_status, report_status;
    double build_started = 0.0;
    if (rule == (Rule *)0)
        rule = find_expanded_rule(target, resolved_target, sizeof(resolved_target));
    else if (!expand_text(rule->target, resolved_target, sizeof(resolved_target), 0)) {
        fprintf(stderr, "qmake: target name expansion failed for '%s'\n", target);
        return 0;
    }
    if (rule == (Rule *)0) {
        implicit_status = implicit_object(target);
        if (implicit_status > 0) return 1;
        if (implicit_status < 0) return 0;
        if (!qmake_file_info(target, &target_time)) {
            fprintf(stderr, "qmake: no rule to make target '%s'\n", target);
            return 0;
        }
        return 1;
    }
    target = resolved_target;
    if (rule->state == 1) {
        fprintf(stderr, "qmake: dependency cycle involving '%s'\n", target);
        return 0;
    }
    if (rule->state == 2) return 1;
    rule->state = 1;
    for (i = 0; i < rule->dep_count; ++i) {
        char dependency[MAX_COMMAND];
        long dependency_time;
        if (!expand_text(rule->deps[i], dependency, sizeof(dependency), 0)) {
            fprintf(stderr, "qmake: prerequisite expansion failed for '%s'\n",
                    rule->deps[i]);
            return 0;
        }
        if (verbosity > 1)
            printf("qmake: checking prerequisite '%s' of '%s'\n",
                   dependency, target);
        if (!build(dependency)) {
            return 0;
        }
        if (qmake_file_info(dependency, &dependency_time) &&
            dependency_time > newest) newest = dependency_time;
    }
    exists = qmake_file_info(target, &target_time);
    if (!exists || (newest >= 0L && newest > target_time)) needs_build = 1;
    report_status = !dry_run && rule->command_count > 0 &&
                    should_report_target(target);
    if (report_status && !needs_build &&
        is_cross_profile_host_helper(target)) report_status = 0;
    if (verbosity > 1)
        printf("qmake: %s '%s'\n", needs_build ? "building" : "up to date:", target);
    if (report_status) {
        if (needs_build) {
            report_target_start(target);
            build_started = qmake_time_seconds();
        } else {
            report_target_start(target);
            report_target_finish(1, 1, 0.0);
        }
    }
    for (recipe = rule->recipes; needs_build && recipe != (Recipe *)0;
         recipe = recipe->next) {
        if (!run_command(recipe->command, target)) {
            if (report_status)
                report_target_finish(0, 0,
                    qmake_time_seconds() - build_started);
            report_recipe_failure(target);
            return 0;
        }
    }
    if (report_status && needs_build)
        report_target_finish(1, 0, qmake_time_seconds() - build_started);
    rule->state = 2;
    return 1;
}

static void reset_description(void)
{
    int i;
    for (i = 0; i < rule_count; ++i) {
        Recipe *recipe = rules[i].recipes;
        while (recipe != (Recipe *)0) {
            Recipe *next = recipe->next;
            free(recipe);
            recipe = next;
        }
    }
    rule_count = 0;
    variable_count = 0;
    errors = 0;
    memset(rules, 0, sizeof(rules));
    memset(variables, 0, sizeof(variables));
    set_variable("HOST_CC", "cc", 0);
    set_variable("HOST_CFLAGS", "-std=c89", 0);
    set_variable("TARGET_CPPFLAGS", "", 0);
    set_variable("TARGET_CFLAGS", "", 0);
    set_variable("TARGET_ASFLAGS", "", 0);
    set_variable("C_TO_R_COMMAND", "", 0);
    set_variable("ASM_TO_R_COMMAND", "", 0);
    set_variable("TARGET_LD", "", 0);
    set_variable("TARGET_LDFLAGS", "", 0);
    set_variable("DEFS", "", 0);
    set_variable("LIBS", "", 0);
    set_variable("STARTUP", "", 0);
    set_variable("TARGET_CPU", "", 0);
}

static int should_report_target(const char *target)
{
    const char *base = target;
    const char *slash;
    const char *backslash;
    size_t length;
    if (strcmp(target, "check-sdk") == 0 || strcmp(target, "help") == 0 ||
        strcmp(target, "all") == 0 || strcmp(target, "clean") == 0 ||
        strcmp(target, "test") == 0) return 0;
    slash = strrchr(base, '/');
    backslash = strrchr(base, '\\');
    if (slash != (const char *)0 && slash + 1 > base) base = slash + 1;
    if (backslash != (const char *)0 && backslash + 1 > base) base = backslash + 1;
    length = strlen(base);
    if ((length > 2 && strcmp(base + length - 2, ".o") == 0) ||
        (length > 2 && strcmp(base + length - 2, ".r") == 0)) return 0;
    return 1;
}

static int is_cross_profile_host_helper(const char *target)
{
    int cross_profile = strcmp(active_section, "mac-xcc-68k") == 0 ||
                        strcmp(active_section, "mac-xqcc-68k") == 0;
    return cross_profile &&
           (strstr(target, "/macOS/ARM64/CMDS/") != (const char *)0 ||
            strstr(target, "/macOS/x86_64/CMDS/") != (const char *)0);
}

static void report_target_start(const char *target)
{
    const char *base = target;
    const char *slash = strrchr(base, '/');
    const char *backslash = strrchr(base, '\\');
    char label[MAX_NAME + MAX_NAME + 8];
    if (slash != (const char *)0 && slash + 1 > base) base = slash + 1;
    if (backslash != (const char *)0 && backslash + 1 > base) base = backslash + 1;
    if (is_cross_profile_host_helper(target))
        sprintf(label, "%s (host) [%s]", base, active_section);
    else
        sprintf(label, "%s [%s]", base, active_section);
    printf("%-34s", label);
    fflush(stdout);
}

static void report_target_finish(int success, int up_to_date, double elapsed)
{
    const char *mark = success ?
        (up_to_date ? "\342\227\217" : "\342\234\223") : "\342\234\227";
    const char *color = success ? "\033[32m" : "\033[31m";
    const char *reset = "\033[0m";
    if (up_to_date)
        printf("           %s%s%s\n", qmake_stdout_is_terminal() ? color : "",
               mark, qmake_stdout_is_terminal() ? reset : "");
    else if (qmake_stdout_is_terminal())
        printf(" %7.2f s %s%s%s\n", elapsed, color, mark, reset);
    else
        printf(" %7.2f s %s\n", elapsed, mark);
    fflush(stdout);
}

static void report_recipe_failure(const char *target)
{
    if (failed_command_output != (FILE *)0) {
        replay_output(failed_command_output, stderr);
        fclose(failed_command_output);
        failed_command_output = (FILE *)0;
    }
    if (failed_command_status != 0)
        fprintf(stderr, "qmake: recipe failed for '%s' (status %d)\n",
                target, failed_command_status);
}

static void replay_output(FILE *input, FILE *output)
{
    char buffer[512];
    size_t count;
    if (input == (FILE *)0 || output == (FILE *)0) return;
    fflush(input);
    rewind(input);
    while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0)
        fwrite(buffer, 1, count, output);
    fflush(output);
}

static int run_command(const char *raw_command, const char *target)
{
    char command[MAX_LINE];
    char capture_command[MAX_LINE + 8];
    FILE *output;
    int status;
    failed_command_output = (FILE *)0;
    failed_command_status = 0;
    if (!expand_text(raw_command, command, sizeof(command), 0)) {
        fprintf(stderr, "qmake: variable expansion overflow/cycle in target '%s'\n", target);
        return 0;
    }
    if (verbosity > 1 || dry_run) printf("%s\n", command);
    if (dry_run) return 1;
    if (strlen(command) + sizeof(" 2>&1") > sizeof(capture_command)) {
        fprintf(stderr, "qmake: command too long to capture output for '%s'\n", target);
        return 0;
    }
    strcpy(capture_command, command);
    strcat(capture_command, " 2>&1");
    output = tmpfile();
    if (output == (FILE *)0) {
        fprintf(stderr, "qmake: cannot create output capture for '%s'; running visibly\n",
                target);
        status = qmake_run(command);
    } else {
        status = qmake_run_capture(capture_command, output);
    }
    if (status != 0) {
        failed_command_output = output;
        failed_command_status = status;
        return 0;
    }
    if (output != (FILE *)0) {
        if (verbosity > 1 || !should_report_target(target))
            replay_output(output, stdout);
        fclose(output);
    }
    return 1;
}

static int implicit_object(const char *target)
{
    const char *suffix = strrchr(target, '.');
    char source[MAX_NAME];
    char command[MAX_LINE];
    long source_time, target_time;
    size_t stem_length;
    int is_c_source, is_host_object;
    if (suffix == (const char *)0) return 0;
    is_host_object = strcmp(suffix, ".o") == 0;
    if (!is_host_object && strcmp(suffix, ".r") != 0) return 0;
    stem_length = (size_t)(suffix - target);
    if (stem_length + 3 >= sizeof(source)) return 0;
    memcpy(source, target, stem_length);
    source[stem_length] = '\0';
    strcat(source, ".c");
    is_c_source = qmake_file_info(source, &source_time);
    if (!is_c_source && !is_host_object) {
        source[stem_length] = '\0';
        strcat(source, ".a");
        if (!qmake_file_info(source, &source_time)) return 0;
    }
    if (!is_c_source && is_host_object) return 0;
    if (qmake_file_info(target, &target_time) && target_time >= source_time) {
        if (verbosity > 0) printf("qmake: '%s' is up to date\n", target);
        return 1;
    }
    if (is_host_object)
        sprintf(command, "$(HOST_CC) $(HOST_CFLAGS) -c -o \"%s\" \"%s\"", target, source);
    else if (is_c_source) {
        const char *template = variable_value("C_TO_R_COMMAND");
        if (variable_value("TARGET_CC")[0] == '\0') {
            fprintf(stderr, "qmake: TARGET_CC is not configured for '%s'\n", target);
            return -1;
        }
        if (template[0] != '\0') {
            if (!expand_implicit_fields(template, source, target, command, sizeof(command))) {
                fprintf(stderr, "qmake: command template too long for '%s'\n", target);
                return -1;
            }
        } else {
            sprintf(command, "$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -c -o \"%s\" \"%s\"", target, source);
        }
    } else {
        const char *template = variable_value("ASM_TO_R_COMMAND");
        if (variable_value("TARGET_AS")[0] == '\0') {
            fprintf(stderr, "qmake: TARGET_AS is not configured for '%s'\n", target);
            return -1;
        }
        if (template[0] != '\0') {
            if (!expand_implicit_fields(template, source, target, command, sizeof(command))) {
                fprintf(stderr, "qmake: command template too long for '%s'\n", target);
                return -1;
            }
        } else {
            sprintf(command, "$(TARGET_AS) $(TARGET_ASFLAGS) \"%s\" \"%s\"", source, target);
        }
    }
    return run_command(command, target) ? 1 : -1;
}

static int implicit_source_exists(const char *target)
{
    const char *suffix = strrchr(target, '.');
    char source[MAX_NAME];
    size_t stem_length;
    long ignored_time;
    if (suffix == (const char *)0 ||
        (strcmp(suffix, ".o") != 0 && strcmp(suffix, ".r") != 0)) return 0;
    stem_length = (size_t)(suffix - target);
    if (stem_length + 3 >= sizeof(source)) return 0;
    memcpy(source, target, stem_length);
    source[stem_length] = '\0';
    strcat(source, ".c");
    if (qmake_file_info(source, &ignored_time)) return 1;
    if (strcmp(suffix, ".r") != 0) return 0;
    source[stem_length] = '\0';
    strcat(source, ".a");
    return qmake_file_info(source, &ignored_time);
}

static int process_directory(const char *requested_target, int depth)
{
    char target[MAX_NAME];
    char subdirs[MAX_COMMAND];
    char *cursor;
    Variable *subdir_var;
    long ignored_time;
    int has_target = requested_target != (const char *)0;
    if (depth >= MAX_DEPTH) {
        fprintf(stderr, "qmake: subdirectory nesting exceeds %d\n", MAX_DEPTH);
        return 0;
    }
    if (has_target) {
        if (strlen(requested_target) >= sizeof(target)) {
            fprintf(stderr, "qmake: target name too long\n");
            return 0;
        }
        strcpy(target, requested_target);
    }
    if (!load_project_description()) return 0;
    {
        const char *required_host = variable_value("HOST_PLATFORM");
        if (required_host[0] != '\0' && strcmp(required_host, host_platform()) != 0) {
            if (selected_section[0] != '\0') {
                fprintf(stderr,
                        "qmake: configuration '%s' requires host '%s' (current host: '%s')\n",
                        active_section, required_host, host_platform());
                return 0;
            }
            if (verbosity > 0)
                printf("qmake: skipping configuration '%s' (requires host %s; current host: %s)\n",
                       active_section, required_host, host_platform());
            return 1;
        }
    }
    /* Help is local to the directory from which qmake was invoked.  Walking
       SUBDIRS for this target makes every nested project's help recipe print
       too, overwhelming the concise top-level overview. */
    subdir_var = has_target && strcmp(target, "help") == 0 ?
                 (Variable *)0 : find_variable("SUBDIRS");
    subdirs[0] = '\0';
    if (subdir_var != (Variable *)0 &&
        !expand_text(variable_value("SUBDIRS"), subdirs, sizeof(subdirs), 0)) {
        fprintf(stderr, "qmake: SUBDIRS expansion failed\n");
        return 0;
    }
    cursor = subdirs;
    while (*cursor != '\0') {
        char child[MAX_NAME];
        char *end;
        size_t length;
        int child_ok;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '\0') break;
        end = cursor;
        while (*end != '\0' && *end != ' ' && *end != '\t') ++end;
        length = (size_t)(end - cursor);
        if (length >= sizeof(child) || length == 0 ||
            (length == 1 && cursor[0] == '.') ||
            (length == 2 && cursor[0] == '.' && cursor[1] == '.') ||
            memchr(cursor, '/', length) != (void *)0 ||
            memchr(cursor, '\\', length) != (void *)0) {
            fprintf(stderr, "qmake: SUBDIRS entries must be direct child directory names\n");
            return 0;
        }
        memcpy(child, cursor, length);
        child[length] = '\0';
        cursor = end;
        if (verbosity > 0) printf("qmake: entering subdirectory '%s'\n", child);
        if (!qmake_change_directory(child)) {
            fprintf(stderr, "qmake: cannot enter subdirectory '%s'\n", child);
            return 0;
        }
        child_ok = process_directory(has_target ? target : (const char *)0, depth + 1);
        if (!qmake_change_directory("..")) {
            fprintf(stderr, "qmake: cannot return from subdirectory '%s'\n", child);
            return 0;
        }
        if (!child_ok) return 0;
    }
    if (!load_project_description()) return 0;
    if (!has_target) {
        if (rule_count == 0) {
            if (section_matched_target) return 1;
            if (depth > 0) return 1;
            fprintf(stderr, "qmake: no default target in configuration '%s'\n",
                    active_section);
            return 0;
        }
        {
            int i, selected = 0;
            for (i = 0; i < rule_count; ++i) {
                if (!rules[i].global_rule) {
                    selected = i;
                    break;
                }
            }
            strcpy(target, rules[selected].target);
        }
        section_matched_target = 1;
    }
    if (has_target) {
        if (find_rule(target) != (Rule *)0 || implicit_source_exists(target) ||
            qmake_file_info(target, &ignored_time)) section_matched_target = 1;
        else return 1;
    }
    return build(target);
}

int main(int argc, char **argv)
{
    const char *target = (const char *)0;
    char global_default_target[MAX_NAME];
    int i, list_configs = 0, attempted = 0, succeeded = 1;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-?") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0) {
            usage(argv[0]); return 0;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--dry-run") == 0)
            dry_run = 1;
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0' ||
                strlen(argv[i + 1]) >= sizeof(project_file)) {
                fprintf(stderr, "qmake: %s requires a makefile path shorter than %lu characters\n",
                        argv[i], (unsigned long)sizeof(project_file));
                return 2;
            }
            strcpy(project_file, argv[++i]);
        }
        else if (strcmp(argv[i], "-C") == 0 || strcmp(argv[i], "--config-dir") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0' ||
                strlen(argv[i + 1]) >= sizeof(config_dir_override)) {
                fprintf(stderr, "qmake: %s requires a config directory\n", argv[i]);
                return 2;
            }
            strcpy(config_dir_override, argv[++i]);
            config_dir_is_set = 1;
        }
        else if (strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--profile") == 0) {
            if (i + 1 >= argc || !valid_path_component(argv[i + 1]) ||
                strlen(argv[i + 1]) >= sizeof(selected_section)) {
                fprintf(stderr, "qmake: %s requires a valid configuration section\n", argv[i]);
                return 2;
            }
            strcpy(selected_section, argv[i + 1]);
            ++i;
        }
        else if (strcmp(argv[i], "--list-configs") == 0) list_configs = 1;
        else if (strcmp(argv[i], "-v") == 0) verbosity = 1;
        else if (strcmp(argv[i], "-vv") == 0) verbosity = 2;
        else if (strncmp(argv[i], "-D", 2) == 0) {
            char definition[MAX_LINE];
            char *equal;
            if (strlen(argv[i] + 2) >= sizeof(definition)) {
                fprintf(stderr, "qmake: definition too long\n"); return 2;
            }
            strcpy(definition, argv[i] + 2);
            equal = strchr(definition, '=');
            if (equal == (char *)0) {
                fprintf(stderr, "qmake: expected -DNAME=value\n"); return 2;
            }
            *equal++ = '\0';
            if (!set_override(definition, equal)) {
                fprintf(stderr, "qmake: invalid/too many command-line variables\n"); return 2;
            }
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "qmake: unknown option '%s'\n", argv[i]);
            usage(argv[0]); return 2;
        } else if (target == (const char *)0) target = argv[i];
        else { fprintf(stderr, "qmake: only one target is supported in this version\n"); return 2; }
    }
    if (!discover_configurations()) return 1;
    if (list_configs) {
        for (i = 0; i < configuration_count; ++i)
            printf("%s\n", configuration_names[i]);
        return 0;
    }
    /* Explicit [global] targets (especially the default help target) run once,
       not once for every selected toolchain section. Section targets still fan out. */
    if (selected_section[0] == '\0') {
        Rule *global_rule;
        active_section[0] = '\0';
        strcpy(active_section, "global");
        if (!load_project_description()) return 1;
        if (target == (const char *)0) {
            for (i = 0; i < rule_count; ++i) {
                if (rules[i].explicit_global_rule) {
                    strcpy(global_default_target, rules[i].target);
                    target = global_default_target;
                    break;
                }
            }
        }
        global_rule = target == (const char *)0 ? (Rule *)0 : find_rule(target);
        if (global_rule != (Rule *)0 && global_rule->explicit_global_rule) {
            section_matched_target = 0;
            if (!process_directory(target, 0)) return 1;
            return section_matched_target ? 0 : 1;
        }
    }
    if (selected_section[0] != '\0') {
        int found = 0;
        for (i = 0; i < configuration_count; ++i)
            if (strcmp(configuration_names[i], selected_section) == 0) found = 1;
        if (!found) {
            fprintf(stderr, "qmake: unknown configuration '%s'\n", selected_section);
            return 2;
        }
        active_section[0] = '\0';
        strcpy(active_section, selected_section);
        section_matched_target = 0;
        if (!process_directory(target, 0)) return 1;
        if (!section_matched_target) {
            fprintf(stderr, "qmake: target '%s' is not defined in configuration '%s'\n",
                    target == (const char *)0 ? "(default)" : target, active_section);
            return 1;
        }
        return 0;
    }
    for (i = 0; i < configuration_count; ++i) {
        if (configuration_count > 1 && strcmp(configuration_names[i], "global") == 0)
            continue;
        strcpy(active_section, configuration_names[i]);
        section_matched_target = 0;
        if (!process_directory(target, 0)) succeeded = 0;
        else if (section_matched_target) ++attempted;
    }
    if (!succeeded) return 1;
    if (attempted == 0) {
        fprintf(stderr, "qmake: target '%s' is not defined by any configuration\n",
                target == (const char *)0 ? "(default)" : target);
        return 1;
    }
    return 0;
}
