#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <limits.h>
#include <locale.h>
#include <errno.h>

// Переменные для опций
int show_links = 0;
int show_dirs = 0;
int show_files = 0;
int sort_output = 0;

// Структура для хранения путей
typedef struct {
    char **items;
    size_t size;
    size_t capacity;
} PathList;

// Инициализация PathList
void init_pathlist(PathList *list) {
    list->items = NULL;
    list->size = 0;
    list->capacity = 0;
}

// Добавление пути в список
void add_path(PathList *list, const char *path) {
    if (list->size == list->capacity) {
        list->capacity = (list->capacity == 0) ? 128 : list->capacity * 2;
        list->items = realloc(list->items, list->capacity * sizeof(char *));
        if (!list->items) {
            perror("Ошибка выделения памяти");
            exit(EXIT_FAILURE);
        }
    }
    list->items[list->size] = strdup(path);
    if (!list->items[list->size]) {
        perror("Ошибка выделения памяти");
        exit(EXIT_FAILURE);
    }
    list->size++;
}

// Освобождение памяти
void free_pathlist(PathList *list) {
    for (size_t i = 0; i < list->size; i++) {
        free(list->items[i]);
    }
    free(list->items);
}

// Функция сравнения для сортировки
int compare_paths(const void *a, const void *b) {
    const char *path1 = *(const char **)a;
    const char *path2 = *(const char **)b;
    return strcoll(path1, path2);
}

// Рекурсивный обход каталогов
void dirwalk(const char *dir_name, PathList *list) {
    DIR *dir;
    struct dirent *entry;

    if ((dir = opendir(dir_name)) == NULL) {
        fprintf(stderr, "Не удалось открыть каталог %s: %s\n", dir_name, strerror(errno));
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        struct stat statbuf;

        // Пропускаем '.' и '..'
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s", dir_name, entry->d_name);

        // Используем lstat для обработки символических ссылок
        if (lstat(path, &statbuf) == -1) {
            fprintf(stderr, "Ошибка получения информации о файле %s: %s\n", path, strerror(errno));
            continue;
        }

        int is_link = S_ISLNK(statbuf.st_mode);
        int is_dir = S_ISDIR(statbuf.st_mode);
        int is_file = S_ISREG(statbuf.st_mode);

        // Проверяем, нужно ли добавлять путь в список
        if ((is_link && show_links) || (is_dir && show_dirs) || (is_file && show_files)) {
            add_path(list, path);
        }

        // Рекурсивно обходим вложенные каталоги
        if (is_dir) {
            dirwalk(path, list);
        }
    }

    closedir(dir);
}

// Обработка опций командной строки
int parse_options(int argc, char *argv[], char **start_dir) {
    int opt;

    // Установка значений по умолчанию
    *start_dir = ".";

    // Пока есть опции для парсинга
    while ((opt = getopt(argc, argv, "ldfs")) != -1) {
        switch (opt) {
            case 'l':
                show_links = 1;
                break;
            case 'd':
                show_dirs = 1;
                break;
            case 'f':
                show_files = 1;
                break;
            case 's':
                sort_output = 1;
                break;
            default:
                fprintf(stderr, "Использование: dirwalk [options] [dir]\n");
                return -1;
        }
    }

    // Проверяем, есть ли необработанные аргументы (начальный каталог)
    if (optind < argc) {
        *start_dir = argv[optind];
    }

    // Если опции l, d, f не указаны, выводим все типы файлов
    if (!show_links && !show_dirs && !show_files) {
        show_links = show_dirs = show_files = 1;
    }

    return 0;
}

// Вывод результатов
void print_paths(PathList *list) {
    for (size_t i = 0; i < list->size; i++) {
        printf("%s\n", list->items[i]);
    }
}

int main(int argc, char *argv[]) {
    char *start_dir;
    PathList paths;

    // Устанавливаем локаль
    setlocale(LC_ALL, "");

    // Обрабатываем опции и определяем начальный каталог
    if (parse_options(argc, argv, &start_dir) != 0) {
        fprintf(stderr, "Использование: dirwalk [options] [dir]\n");
        return EXIT_FAILURE;
    }

    // Инициализируем список путей
    init_pathlist(&paths);

    // Обходим файловую систему
    dirwalk(start_dir, &paths);

    // Сортируем, если требуется
    if (sort_output) {
        qsort(paths.items, paths.size, sizeof(char *), compare_paths);
    }

    // Выводим результаты
    print_paths(&paths);

    // Освобождаем память
    free_pathlist(&paths);

    return EXIT_SUCCESS;
}


