#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include "netpulse/settings.h"

/*
 * The sidebar widget itself needs GTK, so these cover the persistence contract
 * it depends on: default state, round-trip, missing config, and a corrupt key.
 *
 * Note: g_get_user_config_dir() caches its result on first use, so redirecting
 * XDG_CONFIG_HOME per test is not reliable. Each test therefore controls the
 * config file explicitly — removing it, or writing known content — which makes
 * the result independent of whatever the developer's real config contains.
 */

typedef struct {
    char *saved_contents;
} TestEnv;

static TestEnv g_env;

/* Remove any real config so a test starts from a known state. */
static void
config_reset(void)
{
    g_autofree char *path = np_settings_config_path();
    (void)remove(path);
}

static void
env_setup(TestEnv *env, gconstpointer user_data)
{
    (void)user_data;
    g_autofree char *path = np_settings_config_path();
    g_autofree char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_autofree char *saved = NULL;
        if (g_file_get_contents(path, &saved, NULL, NULL))
            env->saved_contents = g_steal_pointer(&saved);
    }
    config_reset();
}

static void
env_teardown(TestEnv *env, gconstpointer user_data)
{
    (void)user_data;
    config_reset();
    g_autofree char *path = np_settings_config_path();
    if (env->saved_contents) {
        g_file_set_contents(path, env->saved_contents, -1, NULL);
        g_clear_pointer(&env->saved_contents, g_free);
    }
}

static void
run_default_is_expanded(TestEnv *env, gconstpointer data)
{
    (void)env;
    (void)data;
    NpSettings s;
    np_settings_init_defaults(&s);
    /* First-time users get the expanded sidebar. */
    g_assert_false(s.sidebar_collapsed);
    np_settings_clear(&s);
}

static void
run_roundtrip(TestEnv *env, gconstpointer data)
{
    (void)env;
    (void)data;
    NpSettings s;
    np_settings_init_defaults(&s);
    s.sidebar_collapsed = TRUE;
    s.sample_interval_ms = 750;

    GError *err = NULL;
    g_assert_true(np_settings_save(&s, &err));
    g_assert_no_error(err);

    NpSettings *loaded = np_settings_load(&err);
    g_assert_no_error(err);
    g_assert_nonnull(loaded);
    g_assert_true(loaded->sidebar_collapsed);
    g_assert_cmpint(loaded->sample_interval_ms, ==, 750);

    np_settings_clear(loaded);
    g_free(loaded);
    np_settings_clear(&s);
}

static void
run_missing_file_defaults(TestEnv *env, gconstpointer data)
{
    (void)env;
    (void)data;
    /* A missing config must yield safe defaults, not a failure. */
    NpSettings *s = np_settings_load(NULL);
    g_assert_nonnull(s);
    g_assert_false(s->sidebar_collapsed);
    g_assert_cmpint(s->monitor_interval_ms, ==, 2000);
    np_settings_clear(s);
    g_free(s);
}

static void
run_corrupt_sidebar_key(TestEnv *env, gconstpointer data)
{
    (void)env;
    (void)data;
    /* An unparsable value must not flip the sidebar unexpectedly. */
    g_autofree char *path = np_settings_config_path();
    g_assert_true(g_file_set_contents(path,
        "[netpulse]\nsidebar-collapsed=not-a-boolean\n", -1, NULL));

    NpSettings *s = np_settings_load(NULL);
    g_assert_nonnull(s);
    g_assert_false(s->sidebar_collapsed);
    np_settings_clear(s);
    g_free(s);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add("/settings/sidebar-default", TestEnv, &g_env, env_setup,
               run_default_is_expanded, env_teardown);
    g_test_add("/settings/roundtrip", TestEnv, &g_env, env_setup,
               run_roundtrip, env_teardown);
    g_test_add("/settings/missing-file", TestEnv, &g_env, env_setup,
               run_missing_file_defaults, env_teardown);
    g_test_add("/settings/corrupt-sidebar-key", TestEnv, &g_env, env_setup,
               run_corrupt_sidebar_key, env_teardown);
    return g_test_run();
}