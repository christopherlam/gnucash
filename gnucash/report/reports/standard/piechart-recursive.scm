;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; account-piecharts.scm: shows piechart of accounts
;;
;; By Robert Merkel (rgmerk@mira.net)
;; and Christian Stimming <stimming@tu-harburg.de>
;;
;; This program is free software; you can redistribute it and/or
;; modify it under the terms of the GNU General Public License as
;; published by the Free Software Foundation; either version 2 of
;; the License, or (at your option) any later version.
;;
;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with this program; if not, contact:
;;
;; Free Software Foundation           Voice:  +1-617-542-5942
;; 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652
;; Boston, MA  02110-1301,  USA       gnu@gnu.org
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-module (gnucash reports standard piechart-recursive))

(use-modules (gnucash engine))
(use-modules (gnucash utilities))
(use-modules (gnucash core-utils))
(use-modules (gnucash app-utils))
(use-modules (gnucash report))
(use-modules (srfi srfi-1))
(use-modules (srfi srfi-9))
(use-modules (ice-9 match))

(define menuname-income (N_ "Income Piechart"))
(define menuname-expense (N_ "Expense Piechart"))
(define menuname-assets (N_ "Asset Piechart"))
(define menuname-securities (N_ "Security Piechart"))
(define menuname-liabilities (N_ "Liability Piechart"))
;; The names are used in the menu

;; The menu statusbar tips.
(define menutip-income
  (N_ "Shows a piechart with the Income per given time interval"))
(define menutip-expense
  (N_ "Shows a piechart with the Expenses per given time interval"))
(define menutip-assets
  (N_ "Shows a piechart with the Assets balance at a given time"))
(define menutip-securities
  (N_ "Shows a piechart with distribution of assets over securities"))
(define menutip-liabilities
  (N_ "Shows a piechart with the Liabilities \
balance at a given time"))

;; The names here are used 1. for internal identification, 2. as
;; tab labels, 3. as default for the 'Report name' option which
;; in turn is used for the printed report title.
(define reportname-income (N_ "Income Accounts"))
(define reportname-expense (N_ "Expense Accounts"))
(define reportname-assets (N_ "Assets"))
(define reportname-securities (N_ "Securities"))
(define reportname-liabilities (N_ "Liabilities"))

(define optname-from-date (N_ "Start Date"))
(define optname-to-date (N_ "End Date"))
(define optname-report-currency (N_ "Report's currency"))
(define optname-price-source (N_ "Price Source"))

(define optname-accounts (N_ "Accounts"))
(define optname-levels (N_ "Show Accounts until level"))

(define optname-fullname (N_ "Show long names"))
(define optname-show-total (N_ "Show Totals"))
(define optname-show-percent (N_ "Show Percents"))
(define optname-slices (N_ "Maximum Slices"))
(define optname-plot-width (N_ "Plot Width"))
(define optname-plot-height (N_ "Plot Height"))
(define optname-sort-method (N_ "Sort Method"))

(define optname-averaging (N_ "Show Average"))
(define opthelp-averaging (N_ "Select whether the amounts should be shown over the full time period or rather as the average e.g. per month."))

;; The option-generator. The only dependence on the type of piechart
;; is the list of account types that the account selection option
;; accepts.
(define (options-generator account-types reverse-balance? do-intervals? depth-based?)
  (let* ((options (gnc:new-options))
         (add-option
          (lambda (new-option)
            (gnc:register-option options new-option))))

    (add-option
     (gnc:make-internal-option "__report" "reverse-balance?" reverse-balance?))

    (if do-intervals?
        (gnc:options-add-date-interval!
         options gnc:pagename-general
         optname-from-date optname-to-date "a")
        (gnc:options-add-report-date!
         options gnc:pagename-general
         optname-to-date "a"))

    (gnc:options-add-currency!
     options gnc:pagename-general optname-report-currency "b")

    (gnc:options-add-price-source!
     options gnc:pagename-general
     optname-price-source "c" 'pricedb-nearest)

    (if do-intervals?
        (add-option
         (gnc:make-multichoice-option
          gnc:pagename-general optname-averaging
          "f" opthelp-averaging
          'None
          (list (vector 'None
                        (N_ "No Averaging")
                        (N_ "Just show the amounts, without any averaging."))
                (vector 'YearDelta
                        (N_ "Yearly")
                        (N_ "Show the average yearly amount during the reporting period."))
                (vector 'MonthDelta
                        (N_ "Monthly")
                        (N_ "Show the average monthly amount during the reporting period."))
                (vector 'WeekDelta
                        (N_ "Weekly")
                        (N_ "Show the average weekly amount during the reporting period."))
                )
          ))
        )

    (add-option
     (gnc:make-account-list-option
      gnc:pagename-accounts optname-accounts
      "a"
      (N_ "Report on these accounts, if chosen account level allows.")
      (lambda ()
        (gnc:filter-accountlist-type
         account-types
         (gnc-account-get-descendants-sorted (gnc-get-current-root-account))))
      (lambda (accounts)
        (list #t
              (gnc:filter-accountlist-type
               account-types
               accounts)))
      #t))

    (if depth-based?
      (gnc:options-add-account-levels!
       options gnc:pagename-accounts optname-levels "b"
       (N_ "Show accounts to this depth and not further.")
       2))

    (add-option
     (gnc:make-simple-boolean-option
      gnc:pagename-display optname-fullname
      "a"
      (if depth-based?
        (N_ "Show the full account name in legend?")
        (N_ "Show the full security name in the legend?"))
      #f))

    (add-option
     (gnc:make-simple-boolean-option
      gnc:pagename-display optname-show-total
      "b" (N_ "Show the total balance in legend?") #t))


     (add-option
      (gnc:make-simple-boolean-option
       gnc:pagename-display optname-show-percent
       "b" (N_ "Show the percentage in legend?") #t))


    (add-option
     (gnc:make-number-range-option
      gnc:pagename-display optname-slices
      "c" (N_ "Maximum number of slices in pie.") 7
      2 24 0 1))

    (gnc:options-add-plot-size!
     options gnc:pagename-display
     optname-plot-width optname-plot-height "d" (cons 'percent 100.0) (cons 'percent 100.0))

    (gnc:options-add-sort-method!
     options gnc:pagename-display
     optname-sort-method "e" 'amount)

    (gnc:options-set-default-section options gnc:pagename-general)

    options))


;; Get display name for account-based reports.
(define (display-name-accounts show-fullname? acc)
  ((if show-fullname?
       gnc-account-get-full-name
       xaccAccountGetName) acc))


;; Sort comparator for account-based reports.
(define (sort-comparator-accounts sort-method show-fullname?)
  (cond
   ((eq? sort-method 'acct-code)
    (lambda (a b)
      (gnc:string-locale<? (xaccAccountGetCode (cadr a))
                           (xaccAccountGetCode (cadr b)))))
   ((eq? sort-method 'alphabetical)
    (lambda (a b)
      (gnc:string-locale<? (display-name-accounts show-fullname? (cadr a))
                           (display-name-accounts show-fullname? (cadr b)))))
   (else
    (lambda (a b) (> (car a) (car b))))))

(define-record-type :slice
  (make-slice account amount color)
  slice?
  (account slice-account)
  (amount slice-amount)
  (color slice-color))

;; The rendering function. Since it works for a bunch of different
;; account settings, you have to give the reportname, the
;; account-types to work on and whether this report works on
;; intervals as arguments.
(define (piechart-renderer report-obj reportname report-guid
                           account-types do-intervals? depth-based?
                           display-name sort-comparator)

  ;; This is a helper function for looking up option values.
  (define (get-option section name)
    (gnc:option-value
     (gnc:lookup-option
      (gnc:report-options report-obj) section name)))

  (gnc:report-starting reportname)

  ;; Get all options
  (let ((to-date (gnc:time64-end-day-time
                     (gnc:date-option-absolute-time
                      (get-option gnc:pagename-general optname-to-date))))
        (from-date (if do-intervals?
                          (gnc:time64-start-day-time
                           (gnc:date-option-absolute-time
                            (get-option gnc:pagename-general
					optname-from-date)))
                          '()))
        (accounts (get-option gnc:pagename-accounts optname-accounts))
        (account-levels
          (if depth-based?
            (get-option gnc:pagename-accounts optname-levels)
            'all))
        (report-currency (get-option gnc:pagename-general
				     optname-report-currency))
        (price-source (get-option gnc:pagename-general
                                  optname-price-source))
        (report-title (get-option gnc:pagename-general
				  gnc:optname-reportname))
        (averaging-selection (if do-intervals?
                                 (get-option gnc:pagename-general
                                             optname-averaging)
                                 'None))

        (show-fullname? (get-option gnc:pagename-display optname-fullname))
        (show-total? (get-option gnc:pagename-display optname-show-total))
        (show-percent? (get-option gnc:pagename-display optname-show-percent))
        (max-slices (inexact->exact
		     (get-option gnc:pagename-display optname-slices)))
        (height (get-option gnc:pagename-display optname-plot-height))
        (width (get-option gnc:pagename-display optname-plot-width))
	(sort-method (get-option gnc:pagename-display optname-sort-method))
	(reverse-balance? (get-option "__report" "reverse-balance?"))

        (document (gnc:make-html-document))
        (chart (gnc:make-html-chart))
        (topl-accounts (gnc:filter-accountlist-type
                        account-types
                        (gnc-account-get-children-sorted
                         (gnc-get-current-root-account)))))

    ;; Returns true if the account a was selected in the account
    ;; selection option.
    (define (show-acct? a)
      (member a accounts))

    ;; Calculates the net balance (profit or loss) of an account
    ;; over the selected reporting period. If subaccts? == #t, all
    ;; subaccount's balances are included as well. Returns a
    ;; commodity-collector.
    (define (profit-fn account subaccts?)
      (if do-intervals?
          (gnc:account-get-comm-balance-interval
           account from-date to-date subaccts?)
          (gnc:account-get-comm-balance-at-date
           account to-date subaccts?)))

    ;; Define more helper variables.
    (let* ((exchange-fn (gnc:case-exchange-fn
                         price-source report-currency to-date))
           (tree-depth (if (equal? account-levels 'all)
                           (gnc:get-current-account-tree-depth)
                           account-levels))
           (averaging-fraction-func (gnc:date-get-fraction-func averaging-selection))
           (averaging-multiplier
            (if averaging-fraction-func
                ;; Calculate the divisor of the amounts so that an
                ;; average is shown
                (let* ((start-frac (averaging-fraction-func from-date))
                       (end-frac (averaging-fraction-func (+ 1 to-date)))
                       (diff (- end-frac start-frac)))
                  ;; Extra sanity check to ensure a positive number
                  (if (> diff 0)
                      (/ 1 diff)
                      1))
                ;; No interval-report, or no averaging interval chosen,
                ;; so just use the multiplier one
                1))
           ;; If there is averaging, the report-title is extended
           ;; accordingly.
           (report-title
            (case averaging-selection
              ((YearDelta) (string-append report-title " " (G_ "Yearly Average")))
              ((MonthDelta) (string-append report-title " " (G_ "Monthly Average")))
              ((WeekDelta) (string-append report-title " " (G_ "Weekly Average")))
              (else report-title)))
           (combined '())
           (other-anchor ""))

      ;; Converts a commodity-collector into one single inexact
      ;; number, depending on the report's currency and the
      ;; exchange-fn calculated above. Returns the absolute value
      ;; multiplied by the averaging-multiplier (smaller than one;
      ;; multiplication instead of division to avoid division-by-zero
      ;; issues) in case the user wants to see the amounts averaged
      ;; over some value.
      (define (collector->amount c)
        ;; Future improvement: Let the user choose which kind of
        ;; currency combining she want to be done. Right now
        ;; everything foreign gets converted
        ;; (gnc:sum-collector-commodity) based on the average
        ;; cost of all holdings.
        (* (gnc:gnc-monetary-amount
            (gnc:sum-collector-commodity c report-currency exchange-fn))
           averaging-multiplier))

      ;; Get balance of an account as an inexact number converted to,
      ;; and using precision of the report's currency.
      (define (account-balance a subaccts?)
        (collector->amount (profit-fn a subaccts?)))

      (gnc:html-chart-set-type! chart 'pie)

      (gnc:html-chart-set-currency-iso!
       chart (gnc-commodity-get-mnemonic report-currency))
      (gnc:html-chart-set-currency-symbol!
       chart (gnc-commodity-get-nice-symbol report-currency))

      (gnc:html-chart-set-title! chart "Title")
      (gnc:html-chart-set-width! chart width)
      (gnc:html-chart-set-height! chart height)

      (let lp0 ((data '()) (depth 0))
        ;; (pk 'lp data depth)
        (cond
         ((= depth tree-depth)
          (let lp3 ((data data) (depth depth))
            (match data
              (() #f)
              ((slices . rest-data)
               (gnc:html-chart-add-data-series!
                chart (number->string depth) (map slice-amount slices)
                (map slice-color slices) 'borderColor 'black)
               (lp3 rest-data (1- depth))))))
         ((= depth 0)
          (lp0 (list (map (lambda (acc color)
                            (make-slice acc (account-balance acc #t) color))
                          topl-accounts
                          (gnc:assign-colors (length topl-accounts))))
               (1+ depth)))
         (else
          (let lp1 ((previous-slices (car data)) (accum '()))
            ;; (pk 'lp1 previous-slices accum)
            (match previous-slices
              (() (lp0 (if (null? accum) data (cons (reverse accum) data)) (1+ depth)))
              ((slice . rest-slices)
               (define color (slice-color slice))
               (define acct (slice-account slice))
               (let lp2 ((children (if acct (gnc-account-get-children acct) '()))
                         (amount-left (slice-amount slice))
                         (slices '()))
                 ;; (pk 'lp2 children amount-left slices)
                 (match children
                   (() (lp1 rest-slices
                            (append (if (zero? amount-left)
                                        '()
                                        (list (make-slice #f amount-left "grey")))
                                    (reverse slices)
                                    accum)))
                   ((child . rest-children)
                    (define child-amount (account-balance child #t))
                    (lp2 rest-children
                         (- amount-left child-amount)
                         (cons (make-slice child child-amount color) slices)))))))))))

      #;
      (gnc:html-chart-add-data-series! chart
                                       "Accounts"
                                       (map round-scu (unzip1 combined))
                                       (gnc:assign-colors (length combined))
                                       'urls urls)
      (gnc:html-chart-set-axes-display! chart #f)

      (gnc:html-document-add-object! document chart)

      (gnc:report-finished)
      document)))

(define (build-report!
          name acct-types income-expense? depth-based? menuname menutip
          reverse-balance? uuid)
  (gnc:define-report
    'version 1
    'name (string-append name " recursive")
    'report-guid (string-append uuid " recursive")
    'menu-path (if income-expense?
                   (list gnc:menuname-income-expense)
                   (list gnc:menuname-asset-liability))
    'menu-name (string-append menuname " recursive")
    'menu-tip menutip
    'options-generator (lambda () (options-generator acct-types
                                                     reverse-balance?
                                                     income-expense?
                                                     depth-based?))
    'renderer (lambda (report-obj)
                (piechart-renderer report-obj name uuid
                                   acct-types income-expense? depth-based?
                                   (if depth-based?
                                       display-name-accounts
                                       display-name-security)
                                   (if depth-based?
                                       sort-comparator-accounts
                                       sort-comparator-security)))))

(build-report!
  reportname-income
  (list ACCT-TYPE-INCOME)
  #t #t
  menuname-income menutip-income
  (lambda (x) #t)
  "e1bd09b8a1dd49dd85760db9d82b045c")

(build-report!
  reportname-expense
  (list ACCT-TYPE-EXPENSE)
  #t #t
  menuname-expense menutip-expense
  (lambda (x) #f)
  "9bf1892805cb4336be6320fe48ce5446")

(build-report!
  reportname-assets
  (list ACCT-TYPE-ASSET ACCT-TYPE-BANK ACCT-TYPE-CASH ACCT-TYPE-CHECKING
        ACCT-TYPE-SAVINGS ACCT-TYPE-MONEYMRKT
        ACCT-TYPE-RECEIVABLE ACCT-TYPE-STOCK ACCT-TYPE-MUTUAL
        ACCT-TYPE-CURRENCY)
  #f #t
  menuname-assets menutip-assets
  (lambda (x) #f)
  "5c7fd8a1fe9a4cd38884ff54214aa88a")

(build-report!
  reportname-securities
  (list ACCT-TYPE-ASSET ACCT-TYPE-BANK ACCT-TYPE-CASH ACCT-TYPE-CHECKING
        ACCT-TYPE-SAVINGS ACCT-TYPE-MONEYMRKT
        ACCT-TYPE-RECEIVABLE ACCT-TYPE-STOCK ACCT-TYPE-MUTUAL
        ACCT-TYPE-CURRENCY)
  #f #f
  menuname-securities menutip-securities
  (lambda (x) #f)
  "e9418ff64f2c11e5b61d1c7508d793ed")

(build-report!
  reportname-liabilities
  (list ACCT-TYPE-LIABILITY ACCT-TYPE-PAYABLE ACCT-TYPE-CREDIT
        ACCT-TYPE-CREDITLINE)
  #f #t
  menuname-liabilities menutip-liabilities
  (lambda (x) #t)
  "3fe6dce77da24c66bdc8f8efdea7f9ac")
