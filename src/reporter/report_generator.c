#include "analyzer_internal.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/*
 * Note on format selection ambiguity:
 * generate_crash_report() accepts only a const char *output_file with no format enum.
 * To adhere strictly to include/engine.h without altering shared headers,
 * the reporter inspects the output_file extension:
 * - If output_file is NULL: formatted plain-text is printed to stdout.
 * - If output_file ends with ".json" (case-insensitive): machine-readable JSON is emitted.
 * - Otherwise: formatted plain-text is written to the destination file.
 */

static bool has_json_extension(const char *path) {
    if (!path) return false;
    size_t len = strlen(path);
    if (len < 5) return false;
    const char *ext = path + len - 5;
    return (ext[0] == '.' &&
            tolower((unsigned char)ext[1]) == 'j' &&
            tolower((unsigned char)ext[2]) == 's' &&
            tolower((unsigned char)ext[3]) == 'o' &&
            tolower((unsigned char)ext[4]) == 'n');
}

/* Escapes characters for strict JSON compliance */
static void write_json_escaped_string(FILE *fp, const char *str) {
    fputc('"', fp);
    if (str) {
        for (const char *p = str; *p; p++) {
            switch (*p) {
                case '"':  fputs("\\\"", fp); break;
                case '\\': fputs("\\\\", fp); break;
                case '\b': fputs("\\b", fp); break;
                case '\f': fputs("\\f", fp); break;
                case '\n': fputs("\\n", fp); break;
                case '\r': fputs("\\r", fp); break;
                case '\t': fputs("\\t", fp); break;
                default:
                    if ((unsigned char)*p < 0x20) {
                        fprintf(fp, "\\u%04x", (unsigned char)*p);
                    } else {
                        fputc(*p, fp);
                    }
                    break;
            }
        }
    }
    fputc('"', fp);
}

static void emit_json_report(FILE *fp, const crash_analysis_result_t *res) {
    fprintf(fp, "{\n");
    fprintf(fp, "  \"probable_cause\": \"%s\",\n", root_cause_to_string(res->probable_cause));
    fprintf(fp, "  \"probable_cause_id\": %d,\n", (int)res->probable_cause);
    fprintf(fp, "  \"confidence_level\": %.4f,\n", res->confidence_level);
    fprintf(fp, "  \"faulting_tid\": %u,\n", (unsigned int)res->faulting_tid);
    fprintf(fp, "  \"faulting_addr\": \"0x%llx\",\n", (unsigned long long)res->faulting_addr);

    fprintf(fp, "  \"explanation\": ");
    write_json_escaped_string(fp, res->explanation);
    fprintf(fp, ",\n");

    fprintf(fp, "  \"causal_chain_len\": %zu,\n", res->causal_chain_len);
    fprintf(fp, "  \"causal_chain\": [\n");

    for (size_t i = 0; i < res->causal_chain_len; i++) {
        const crash_event_t *ev = &res->causal_chain[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"sequence_id\": %llu,\n", (unsigned long long)ev->sequence_id);
        fprintf(fp, "      \"timestamp_ns\": %llu,\n", (unsigned long long)ev->timestamp_ns);
        fprintf(fp, "      \"pid\": %u,\n", (unsigned int)ev->pid);
        fprintf(fp, "      \"tid\": %u,\n", (unsigned int)ev->tid);
        fprintf(fp, "      \"category\": \"%s\",\n", event_category_to_string(ev->category));
        fprintf(fp, "      \"type\": \"%s\",\n", event_type_to_string(ev->type));
        fprintf(fp, "      \"addr\": \"0x%llx\",\n", (unsigned long long)ev->addr);
        fprintf(fp, "      \"size\": %zu,\n", ev->size);
        fprintf(fp, "      \"fd_or_status\": %d,\n", (int)ev->fd_or_status);
        fprintf(fp, "      \"label\": ");
        write_json_escaped_string(fp, ev->label);
        fprintf(fp, "\n    }%s\n", (i + 1 < res->causal_chain_len) ? "," : "");
    }

    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
}

static void emit_text_report(FILE *fp, const crash_analysis_result_t *res) {
    fprintf(fp, "================================================================================\n");
    fprintf(fp, "                     CRASH ARCHAEOLOGIST 2.0 REPORT\n");
    fprintf(fp, "================================================================================\n");
    fprintf(fp, "Probable Cause   : %s\n", root_cause_to_string(res->probable_cause));
    fprintf(fp, "Confidence Level : %.1f%% (Heuristic Estimate)\n", res->confidence_level * 100.0f);
    fprintf(fp, "Faulting Thread  : %u\n", (unsigned int)res->faulting_tid);
    fprintf(fp, "Faulting Address : 0x%llx\n", (unsigned long long)res->faulting_addr);
    fprintf(fp, "Explanation      : %s\n", res->explanation);
    fprintf(fp, "--------------------------------------------------------------------------------\n");
    fprintf(fp, "Causal Chain (%zu events):\n", res->causal_chain_len);

    if (res->causal_chain_len == 0) {
        fprintf(fp, "  (No preceding correlated events recorded)\n");
    } else {
        for (size_t i = 0; i < res->causal_chain_len; i++) {
            const crash_event_t *ev = &res->causal_chain[i];
            fprintf(fp, "  [%02zu] Seq: %-5llu | TID: %-4u | %-20s | %-22s | Addr: 0x%-10llx | Size: %-5zu | FD/Stat: %-4d | Label: %s\n",
                    i,
                    (unsigned long long)ev->sequence_id,
                    (unsigned int)ev->tid,
                    event_category_to_string(ev->category),
                    event_type_to_string(ev->type),
                    (unsigned long long)ev->addr,
                    ev->size,
                    (int)ev->fd_or_status,
                    ev->label[0] ? ev->label : "-");
        }
    }
    fprintf(fp, "================================================================================\n");
}

int generate_crash_report(const crash_analysis_result_t *result, const char *output_file) {
    if (!result) {
        return -1;
    }

    if (!output_file) {
        emit_text_report(stdout, result);
        return 0;
    }

    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        return -1;
    }

    if (has_json_extension(output_file)) {
        emit_json_report(fp, result);
    } else {
        emit_text_report(fp, result);
    }

    fclose(fp);
    return 0;
}
