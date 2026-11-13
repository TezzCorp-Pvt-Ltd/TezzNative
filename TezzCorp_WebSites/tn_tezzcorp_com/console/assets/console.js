/**
 * console/assets/console.js — TezzNative Console Interactivity & Utilities
 */

document.addEventListener('DOMContentLoaded', function () {
  // 1. Mobile Sidebar Drawer & Overlay Management
  const mobileToggle = document.getElementById('mobileNavToggle');
  const sidebar = document.getElementById('consoleSidebar');
  const backdrop = document.getElementById('sidebarBackdrop');
  const closeBtn = document.getElementById('sidebarCloseBtn');

  function openSidebar() {
    if (sidebar) sidebar.classList.add('open');
    if (backdrop) backdrop.classList.add('active');
    document.body.style.overflow = 'hidden';
  }

  function closeSidebar() {
    if (sidebar) sidebar.classList.remove('open');
    if (backdrop) backdrop.classList.remove('active');
    document.body.style.overflow = '';
  }

  if (mobileToggle && sidebar) {
    mobileToggle.addEventListener('click', function (e) {
      e.stopPropagation();
      if (sidebar.classList.contains('open')) {
        closeSidebar();
      } else {
        openSidebar();
      }
    });
  }

  if (closeBtn) {
    closeBtn.addEventListener('click', function (e) {
      e.stopPropagation();
      closeSidebar();
    });
  }

  if (backdrop) {
    backdrop.addEventListener('click', function () {
      closeSidebar();
    });
  }

  // Auto-close sidebar on mobile nav link click
  if (sidebar) {
    sidebar.querySelectorAll('.sidebar-item').forEach(function (link) {
      link.addEventListener('click', function () {
        if (window.innerWidth <= 900) {
          closeSidebar();
        }
      });
    });
  }

  // 2. Modal attribute triggers
  document.querySelectorAll('[data-modal-open]').forEach(function (trigger) {
    trigger.addEventListener('click', function () {
      const modalId = trigger.getAttribute('data-modal-open');
      window.openModal(modalId);
    });
  });

  document.querySelectorAll('[data-modal-close], .modal-close').forEach(function (btn) {
    btn.addEventListener('click', function () {
      const modal = btn.closest('.c-modal-overlay, .modal-backdrop');
      if (modal) modal.classList.remove('active');
    });
  });

  // Close modal when clicking on backdrop
  document.querySelectorAll('.c-modal-overlay, .modal-backdrop').forEach(function (overlay) {
    overlay.addEventListener('click', function (e) {
      if (e.target === overlay) {
        overlay.classList.remove('active');
      }
    });
  });

  // Close on Escape key
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') {
      document.querySelectorAll('.c-modal-overlay.active, .modal-backdrop.active').forEach(function (m) {
        m.classList.remove('active');
      });
      closeSidebar();
    }
  });

  // 3. Theme Toggle (Day / Night Mode)
  const themeToggle = document.getElementById('consoleThemeToggle');
  if (themeToggle) {
    themeToggle.addEventListener('click', function () {
      const isLight = document.documentElement.classList.contains('light');
      if (isLight) {
        document.documentElement.classList.remove('light');
        document.documentElement.classList.add('dark');
        localStorage.setItem('tn-theme', 'dark');
      } else {
        document.documentElement.classList.remove('dark');
        document.documentElement.classList.add('light');
        localStorage.setItem('tn-theme', 'light');
      }
    });
  }

  // 4. Dynamic Real-time Table Filter (fallback for simple tables)
  const filterInputs = document.querySelectorAll('.table-filter-input:not([data-managed-dt])');
  filterInputs.forEach(function (input) {
    input.addEventListener('input', function () {
      const query = input.value.trim().toLowerCase();
      const rows = document.querySelectorAll('.filterable-row, .data-table tbody tr, .console-table tbody tr');
      rows.forEach(function (row) {
        if (row.cells && row.cells.length === 1 && row.cells[0].colSpan > 1) return;
        const text = row.innerText.toLowerCase();
        row.style.display = text.includes(query) ? '' : 'none';
      });
    });
  });

  // 5. Button Loaders for Console Forms & Submissions
  document.addEventListener('submit', function (e) {
    const form = e.target;
    if (!form || form.tagName !== 'FORM') return;

    if (typeof form.checkValidity === 'function' && !form.checkValidity()) {
      return;
    }

    const btn = form.querySelector('button[type="submit"], input[type="submit"], .btn-submit, button:not([type="button"]):not([type="reset"])');
    if (!btn || btn.classList.contains('is-loading')) return;

    btn.classList.add('is-loading');

    if (!btn.querySelector('.btn-spinner')) {
      const spinner = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      spinner.setAttribute('class', 'btn-spinner');
      spinner.setAttribute('width', '16');
      spinner.setAttribute('height', '16');
      spinner.setAttribute('viewBox', '0 0 24 24');
      spinner.setAttribute('fill', 'none');
      spinner.setAttribute('stroke', 'currentColor');
      spinner.setAttribute('stroke-width', '2.5');
      spinner.innerHTML = '<path d="M21 12a9 9 0 1 1-6.219-8.56"/>';

      if (btn.tagName === 'INPUT') {
        btn.dataset.origVal = btn.value;
        btn.value = 'Processing...';
      } else {
        btn.insertBefore(spinner, btn.firstChild);
      }
    }
  });

  document.addEventListener('click', function (e) {
    const trigger = e.target.closest('[data-loading], .btn-action-load');
    if (!trigger || trigger.classList.contains('is-loading')) return;
    trigger.classList.add('is-loading');
    if (!trigger.querySelector('.btn-spinner')) {
      const sp = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      sp.setAttribute('class', 'btn-spinner');
      sp.setAttribute('width', '16');
      sp.setAttribute('height', '16');
      sp.setAttribute('viewBox', '0 0 24 24');
      sp.setAttribute('fill', 'none');
      sp.setAttribute('stroke', 'currentColor');
      sp.setAttribute('stroke-width', '2.5');
      sp.innerHTML = '<path d="M21 12a9 9 0 1 1-6.219-8.56"/>';
      trigger.insertBefore(sp, trigger.firstChild);
    }
  });
});

/**
 * Native DataTable Engine (Zero External CDN Dependency)
 * Provides client-side search, category filter, sorting, and pagination.
 */
window.initConsoleDataTable = function (config) {
  const table = document.getElementById(config.tableId);
  if (!table) return;

  const tbody = table.querySelector('tbody');
  if (!tbody) return;

  const allRows = Array.from(tbody.querySelectorAll('tr'));
  let filteredRows = [...allRows];
  let currentPage = 1;
  let pageSize = parseInt(config.defaultPageSize || 10, 10);
  let sortColIndex = -1;
  let sortAsc = true;

  const searchInput = config.searchId ? document.getElementById(config.searchId) : null;
  const categorySelect = config.categoryId ? document.getElementById(config.categoryId) : null;
  const pageSizeSelect = config.pageSizeId ? document.getElementById(config.pageSizeId) : null;
  const paginationContainer = config.paginationId ? document.getElementById(config.paginationId) : null;
  const infoContainer = config.infoId ? document.getElementById(config.infoId) : null;

  function filterAndSort() {
    const query = searchInput ? searchInput.value.trim().toLowerCase() : '';
    const cat = categorySelect ? categorySelect.value.trim().toLowerCase() : '';

    filteredRows = allRows.filter(function (row) {
      if (row.cells.length === 1 && row.cells[0].colSpan > 1) return false;
      const text = row.innerText.toLowerCase();
      const matchesSearch = !query || text.includes(query);
      const rowCategory = (row.getAttribute('data-category') || row.cells[2]?.innerText || '').toLowerCase();
      const matchesCat = !cat || rowCategory.includes(cat);
      return matchesSearch && matchesCat;
    });

    if (sortColIndex >= 0) {
      filteredRows.sort(function (a, b) {
        let valA = a.cells[sortColIndex]?.getAttribute('data-sort') || a.cells[sortColIndex]?.innerText.trim() || '';
        let valB = b.cells[sortColIndex]?.getAttribute('data-sort') || b.cells[sortColIndex]?.innerText.trim() || '';
        const numA = parseFloat(valA.replace(/[^0-9.-]/g, ''));
        const numB = parseFloat(valB.replace(/[^0-9.-]/g, ''));
        if (!isNaN(numA) && !isNaN(numB)) {
          return sortAsc ? numA - numB : numB - numA;
        }
        return sortAsc ? valA.localeCompare(valB) : valB.localeCompare(valA);
      });
    }

    currentPage = 1;
    render();
  }

  function render() {
    const total = filteredRows.length;
    const isAll = pageSize <= 0;
    const totalPages = isAll ? 1 : Math.ceil(total / pageSize);

    if (currentPage > totalPages) currentPage = Math.max(1, totalPages);

    const start = isAll ? 0 : (currentPage - 1) * pageSize;
    const end = isAll ? total : Math.min(start + pageSize, total);

    // Hide all rows
    allRows.forEach(r => r.style.display = 'none');

    // Show current page slice
    filteredRows.slice(start, end).forEach(r => r.style.display = '');

    // Update info
    if (infoContainer) {
      if (total === 0) {
        infoContainer.innerText = 'No matching entries found';
      } else {
        infoContainer.innerText = `Showing ${start + 1} to ${end} of ${total} entries` + (total !== allRows.length ? ` (filtered from ${allRows.length} total)` : '');
      }
    }

    // Render pagination buttons
    if (paginationContainer) {
      paginationContainer.innerHTML = '';
      if (totalPages <= 1) return;

      const prevBtn = document.createElement('button');
      prevBtn.className = 'page-btn';
      prevBtn.innerHTML = '&lsaquo; Prev';
      prevBtn.disabled = (currentPage === 1);
      prevBtn.onclick = () => { if (currentPage > 1) { currentPage--; render(); } };
      paginationContainer.appendChild(prevBtn);

      for (let p = 1; p <= totalPages; p++) {
        if (p === 1 || p === totalPages || (p >= currentPage - 2 && p <= currentPage + 2)) {
          const btn = document.createElement('button');
          btn.className = 'page-btn' + (p === currentPage ? ' active' : '');
          btn.innerText = p;
          btn.onclick = () => { currentPage = p; render(); };
          paginationContainer.appendChild(btn);
        } else if (p === currentPage - 3 || p === currentPage + 3) {
          const ellipsis = document.createElement('span');
          ellipsis.className = 'pagination-ellipsis';
          ellipsis.innerText = '…';
          ellipsis.style.padding = '0 4px';
          paginationContainer.appendChild(ellipsis);
        }
      }

      const nextBtn = document.createElement('button');
      nextBtn.className = 'page-btn';
      nextBtn.innerHTML = 'Next &rsaquo;';
      nextBtn.disabled = (currentPage === totalPages);
      nextBtn.onclick = () => { if (currentPage < totalPages) { currentPage++; render(); } };
      paginationContainer.appendChild(nextBtn);
    }
  }

  // Column header sorting
  const headers = table.querySelectorAll('thead th[data-sortable]');
  headers.forEach((th, idx) => {
    th.classList.add('sortable-th');
    const actualColIndex = th.cellIndex;
    th.innerHTML = th.innerHTML + '<span class="sort-icon">&varr;</span>';
    th.addEventListener('click', () => {
      if (sortColIndex === actualColIndex) {
        sortAsc = !sortAsc;
      } else {
        sortColIndex = actualColIndex;
        sortAsc = true;
      }
      headers.forEach(h => {
        h.classList.remove('sort-asc', 'sort-desc');
        const icon = h.querySelector('.sort-icon');
        if (icon) icon.innerHTML = '&varr;';
      });
      th.classList.add(sortAsc ? 'sort-asc' : 'sort-desc');
      const curIcon = th.querySelector('.sort-icon');
      if (curIcon) curIcon.innerHTML = sortAsc ? '&uarr;' : '&darr;';
      filterAndSort();
    });
  });

  if (searchInput) searchInput.addEventListener('input', filterAndSort);
  if (categorySelect) categorySelect.addEventListener('change', filterAndSort);
  if (pageSizeSelect) pageSizeSelect.addEventListener('change', () => {
    pageSize = parseInt(pageSizeSelect.value, 10);
    filterAndSort();
  });

  filterAndSort();
};

// Global Modal Functions
window.openModal = function (modalId) {
  const modal = document.getElementById(modalId);
  if (modal) {
    modal.classList.add('active');
    const firstInput = modal.querySelector('input:not([type="hidden"]), select, textarea');
    if (firstInput) {
      setTimeout(() => firstInput.focus(), 80);
    }
  }
};

window.closeModal = function (modalId) {
  const modal = document.getElementById(modalId);
  if (modal) {
    modal.classList.remove('active');
  }
};

// Clipboard copy helper
function copyToClipboard(text, btn) {
  if (!navigator.clipboard) {
    const el = document.createElement('textarea');
    el.value = text;
    document.body.appendChild(el);
    el.select();
    document.execCommand('copy');
    document.body.removeChild(el);
    if (btn) {
      const orig = btn.innerText;
      btn.innerText = 'Copied!';
      setTimeout(function () { btn.innerText = orig; }, 1800);
    }
    return;
  }

  navigator.clipboard.writeText(text).then(function () {
    if (btn) {
      const orig = btn.innerText;
      btn.innerText = 'Copied!';
      setTimeout(function () { btn.innerText = orig; }, 1800);
    }
  });
}
