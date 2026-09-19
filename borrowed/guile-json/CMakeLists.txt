# Only the builder half of guile-json is used, by gnucash/report/html-chart.scm,
# which calls scm->json-string to serialise the Chart.js options object. The
# parser and syntax modules, and the (gnucash json) module that re-exported
# them, had no callers and were dropped.

set (scm-guile-json-details
  json/builder.scm
)

gnc_add_scheme_targets (guile-json
    SOURCES "${scm-guile-json-details}"
    OUTPUT_DIR gnucash/json
    MAKE_LINKS)

set_local_dist(guile-json_DIST_LOCAL
  AUTHORS
  CMakeLists.txt
  COPYING
  ${scm-guile-json-details}
  NEWS
  README.org
)

set(guile-json_DIST ${guile-json_DIST_LOCAL} PARENT_SCOPE)
