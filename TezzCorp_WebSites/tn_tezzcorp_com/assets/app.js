/**
 * assets/app.js — TezzNative Web Portal (Production JS)
 * Engineered for TezzCorp Pvt Ltd. | Created by Rohit Pathak
 * Version: 2.2.1 | 100% Pure Vanilla JS — Zero External Dependencies
 */

(function () {
  'use strict';

  // ── 1. Theme Management (Light / Dark Mode) ──────────────────────────────────
  function initTheme() {
    var root = document.documentElement;
    var stored = localStorage.getItem('tn_theme') || 'dark';
    root.classList.toggle('light', stored === 'light');
    root.classList.toggle('dark',  stored !== 'light');

    function toggleMode(e) {
      if (e) e.preventDefault();
      var isLight = root.classList.contains('light');
      root.classList.toggle('light', !isLight);
      root.classList.toggle('dark',   isLight);
      localStorage.setItem('tn_theme', isLight ? 'dark' : 'light');
    }

    var btns = document.querySelectorAll('.theme-toggle');
    btns.forEach(function (btn) {
      btn.addEventListener('click', toggleMode);
    });
  }

  // ── 2. Mobile Navigation & Off-Canvas Sidebar ────────────────────────────────
  function initMobileNav() {
    var toggleBtn  = document.getElementById('mobileToggle');
    var sidebar    = document.getElementById('mobileSidebar');
    var overlay    = document.getElementById('sidebarOverlay');
    var closeBtn   = document.getElementById('sidebarClose');
    var navLinks   = document.querySelectorAll('.sidebar-link, .sidebar-btn-install');

    if (!toggleBtn || !sidebar) return;

    function openSidebar() {
      sidebar.classList.add('open');
      if (overlay) overlay.classList.add('active');
      toggleBtn.setAttribute('aria-expanded', 'true');
      sidebar.setAttribute('aria-hidden', 'false');
      if (overlay) overlay.setAttribute('aria-hidden', 'false');
      document.body.classList.add('sidebar-locked');
    }

    function closeSidebar() {
      sidebar.classList.remove('open');
      if (overlay) overlay.classList.remove('active');
      toggleBtn.setAttribute('aria-expanded', 'false');
      sidebar.setAttribute('aria-hidden', 'true');
      if (overlay) overlay.setAttribute('aria-hidden', 'true');
      document.body.classList.remove('sidebar-locked');
    }

    toggleBtn.addEventListener('click', function (e) {
      e.preventDefault();
      e.stopPropagation();
      sidebar.classList.contains('open') ? closeSidebar() : openSidebar();
    });

    if (closeBtn) {
      closeBtn.addEventListener('click', function (e) {
        e.preventDefault();
        closeSidebar();
      });
    }

    if (overlay) {
      overlay.addEventListener('click', closeSidebar);
    }

    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape' && sidebar.classList.contains('open')) {
        closeSidebar();
      }
    });

    navLinks.forEach(function (link) {
      link.addEventListener('click', function () {
        closeSidebar();
      });
    });
  }

  // ── 3. Sticky Header Shadow on Scroll ────────────────────────────────────────
  function initHeaderScroll() {
    var header = document.getElementById('mainHeader') || document.querySelector('.main-header');
    if (!header) return;
    function onScroll() {
      if (window.scrollY > 20) {
        header.style.boxShadow = '0 4px 24px rgba(0,0,0,.45)';
      } else {
        header.style.boxShadow = '';
      }
    }
    window.addEventListener('scroll', onScroll, { passive: true });
    onScroll();
  }

  // ── 4. Copy-to-Clipboard ──────────────────────────────────────────────────────
  function initCopyButtons() {
    document.addEventListener('click', function (e) {
      var btn = e.target.closest('.copy-btn, .btn-copy, [data-copy]');
      if (!btn) return;
      e.preventDefault();

      var text = btn.getAttribute('data-copy');
      if (!text) {
        var code = btn.parentElement ? btn.parentElement.querySelector('code, pre') : null;
        if (code) text = code.textContent;
      }

      if (!text || !navigator.clipboard) return;

      navigator.clipboard.writeText(text.trim()).then(function () {
        var orig = btn.innerHTML;
        btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#10b981" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"/></svg>';
        btn.style.color = '#10b981';
        setTimeout(function () { btn.innerHTML = orig; btn.style.color = ''; }, 2000);
      }).catch(function (err) { console.warn('Copy failed:', err); });
    });
  }

  // ── 5. Code Playground Tab Switcher ─────────────────────────────────────────
  var codeDemos = {
    async: {
      title: "Concurrent Actor Tasks & Mailboxes",
      code:
`// Concurrent Worker Tasks & Mailboxes in TezzNative
import "actor"
import "net"

fn handle_worker(sys:*ActorSystem, worker_id:int, master_id:int, uid:int) -> int:
  say "Spawning worker task for UID: ", uid
  resp:str = "HTTP/1.1 200 OK\\r\\nContent-Type: application/json\\r\\n\\r\\n{\\"user\\":\\"ok\\"}"
  status:int = net.http_status_code(resp)
  // Send status back to master actor mailbox
  ret actor.send_from(sys, worker_id, master_id, 1, uid, status, 0, 0, "ok")

fn main() -> int:
  say "Spawning concurrent actor worker tasks..."
  sys:*ActorSystem = actor.system_new(4, 4, 1)
  if sys == 0:
    ret 1

  master:int = actor.spawn(sys, "master")
  w1:int = actor.spawn(sys, "worker-1")
  w2:int = actor.spawn(sys, "worker-2")

  handle_worker(sys, w1, master, 101)
  handle_worker(sys, w2, master, 102)

  msg1:ActorMessage
  msg2:ActorMessage
  unsafe:
    actor.receive(sys, master, &msg1)
    actor.receive(sys, master, &msg2)
    say "Worker 1 HTTP status: ", msg1.arg2
    say "Worker 2 HTTP status: ", msg2.arg2

  actor.system_free(sys)
  ret 0`,
      output: [
        { text: "[tezzc] Compiling async_demo.tn → x86-64 native PE...", cls: "muted" },
        { text: "[tezzc] Binary generated: async_demo.exe (18.4 KB) in 3.4ms", cls: "muted" },
        { text: "Spawning concurrent actor worker tasks...", cls: "" },
        { text: "Spawning worker task for UID: 101", cls: "" },
        { text: "Spawning worker task for UID: 102", cls: "" },
        { text: "Worker 1 HTTP status: 200", cls: "success" },
        { text: "Worker 2 HTTP status: 200", cls: "success" },
        { text: "[process exited code 0 in 1.1ms | Peak RAM: 1.4 MB]", cls: "perf" }
      ]
    },
    xor: {
      title: "Dense Matrix Math & Softmax Layer",
      code:
`// Dense Matrix Math & Softmax Activation in TezzNative
import "llm_core"

fn main() -> int:
  say "Allocating input activations & projection weights..."
  unsafe:
    raw_a:*char = malloc(4 * 8)
    raw_b:*char = malloc(4 * 8)
    raw_c:*char = malloc(4 * 8)

    a:*float = raw_a as *float
    b:*float = raw_b as *float
    c:*float = raw_c as *float

    // 2x2 Input Tensor
    a[0] = 1.0; a[1] = 2.0
    a[2] = 3.0; a[3] = 4.0

    // 2x2 Weight Matrix
    b[0] = 0.5; b[1] = 0.0
    b[2] = 0.0; b[3] = 0.5

    // Forward GEMM: C = A @ B
    llm_core.matmul_f64(c, a, b, 2, 2, 2)
    say "Projected C[0]: ", c[0]

    // Numerically stable Softmax layer
    llm_core.softmax_rows_f64(c, c, 2, 2)
    say "Softmax probability[0]: ", c[0]

    free(raw_a)
    free(raw_b)
    free(raw_c)

  say "Forward pass completed successfully."
  ret 0`,
      output: [
        { text: "[tezzc] Compiling matrix_demo.tn → x86-64 native (AVX2)...", cls: "muted" },
        { text: "[tezzc] Binary generated: matrix_demo.exe (21.2 KB) in 3.1ms", cls: "muted" },
        { text: "Allocating input activations & projection weights...", cls: "" },
        { text: "Projected C[0]: 0.500000", cls: "success" },
        { text: "Softmax probability[0]: 0.377541", cls: "success" },
        { text: "Forward pass completed successfully.", cls: "success" },
        { text: "[process exited code 0 in 0.8ms | Peak RAM: 1.8 MB]", cls: "perf" }
      ]
    },
    gguf: {
      title: "INT8 Quantized Inference & Decoder",
      code:
`// Quantized INT8 Inference & Decoder in TezzNative
import "llm_core"

fn main() -> int:
  say "Loading quantized INT8 model weights..."
  unsafe:
    raw_x:*char = malloc(4 * 8)
    raw_w:*char = malloc(8)
    raw_s:*char = malloc(2 * 8)
    raw_out:*char = malloc(2 * 8)

    x:*float = raw_x as *float
    w:*i8 = raw_w as *i8
    scales:*float = raw_s as *float
    out:*float = raw_out as *float

    // Input hidden state vector
    x[0] = 1.0; x[1] = 0.5; x[2] = 2.0; x[3] = 1.5

    // Per-channel quantization scales
    scales[0] = 0.05; scales[1] = 0.02

    // Quantized INT8 weights (4x2 matrix)
    raw_w[0] = 10 as char; raw_w[1] = 20 as char
    raw_w[2] = 5 as char;  raw_w[3] = 15 as char
    raw_w[4] = 30 as char; raw_w[5] = 10 as char
    raw_w[6] = 25 as char; raw_w[7] = 40 as char

    // Perform hardware-friendly INT8 GEMV
    llm_core.matmul_i8_f64(out, x, w, scales, 1, 2, 4)
    say "Logits: [", out[0], ", ", out[1], "]"

    // Sample next token ID via greedy decoder
    next_tok:int = llm_core.tiny_decoder_next_token(out, 2)
    say "Predicted Next Token ID: ", next_tok

    free(raw_x)
    free(raw_w)
    free(raw_s)
    free(raw_out)

  say "Quantized inference finished."
  ret 0`,
      output: [
        { text: "[tezzc] Compiling quant_demo.tn → x86-64 native...", cls: "muted" },
        { text: "[tezzc] Binary generated: quant_demo.exe (22.5 KB) in 3.6ms", cls: "muted" },
        { text: "Loading quantized INT8 model weights...", cls: "" },
        { text: "Logits: [5.500000, 2.150000]", cls: "success" },
        { text: "Predicted Next Token ID: 0", cls: "success" },
        { text: "Quantized inference finished.", cls: "success" },
        { text: "[process exited code 0 in 1.2ms | Peak RAM: 2.1 MB]", cls: "perf" }
      ]
    },
    defer_sugar: {
      title: "Systems Bump Arena Allocator",
      code:
`// Systems Bump/Arena Allocator in TezzNative
import "arena"

fn main() -> int:
  say "Allocating 64KB Bump Arena for batch pipeline..."
  a:*Arena = arena.arena_new(65536)
  if a == 0:
    ret 1

  // Fast contiguous bump allocations
  buf:*char = arena.arena_alloc(a, 1024)
  msg:str = arena.arena_strdup(a, "Zero-Fragmentation Systems Buffer")
  say "Arena Token: ", msg
  say "Bytes Used: ", arena.arena_used(a)
  say "Bytes Remaining: ", arena.arena_remaining(a)

  // O(1) Instantaneous memory reset
  arena.arena_reset(a)
  say "Arena reset complete. Bytes Used: ", arena.arena_used(a)

  arena.arena_free(a)
  say "Arena memory released."
  ret 0`,
      output: [
        { text: "[tezzc] Compiling arena_demo.tn → x86-64 native...", cls: "muted" },
        { text: "[tezzc] Binary generated: arena_demo.exe (19.8 KB) in 2.9ms", cls: "muted" },
        { text: "Allocating 64KB Bump Arena for batch pipeline...", cls: "" },
        { text: "Arena Token: Zero-Fragmentation Systems Buffer", cls: "success" },
        { text: "Bytes Used: 1058", cls: "success" },
        { text: "Bytes Remaining: 64478", cls: "success" },
        { text: "Arena reset complete. Bytes Used: 0", cls: "success" },
        { text: "Arena memory released.", cls: "muted" },
        { text: "[process exited code 0 in 0.6ms | Peak RAM: 1.1 MB]", cls: "perf" }
      ]
    },
    http_serve: {
      title: "High-Throughput HTTP Routing",
      code:
`// High-Throughput HTTP Request Routing & Parsing
import "net"

fn route_request(req:str) -> int:
  method:str = net.http_method(req)
  path:str = net.http_route_path(req)
  say "HTTP Request -> [", method, "] ", path

  if net._starts_with(path, "/api/v1/health") != 0:
    say "Status: 200 OK (Content-Type: application/json)"
  else:
    say "Status: 404 Not Found"

  unsafe:
    free(method)
    free(path)
  ret 0

fn main() -> int:
  say "TezzNative High-Performance HTTP Router"
  req1:str = "GET /api/v1/health HTTP/1.1\\r\\nHost: tezznative.org\\r\\n\\r\\n"
  req2:str = "POST /api/v1/unknown HTTP/1.1\\r\\nHost: tezznative.org\\r\\n\\r\\n"

  route_request(req1)
  route_request(req2)
  say "All HTTP routes dispatched successfully."
  ret 0`,
      output: [
        { text: "[tezzc] Compiling http_router.tn → x86-64 native...", cls: "muted" },
        { text: "[tezzc] Binary generated: http_router.exe (23.1 KB) in 3.2ms", cls: "muted" },
        { text: "TezzNative High-Performance HTTP Router", cls: "muted" },
        { text: "HTTP Request -> [GET] /api/v1/health", cls: "" },
        { text: "Status: 200 OK (Content-Type: application/json)", cls: "success" },
        { text: "HTTP Request -> [POST] /api/v1/unknown", cls: "" },
        { text: "Status: 404 Not Found", cls: "success" },
        { text: "All HTTP routes dispatched successfully.", cls: "success" },
        { text: "[bench] wrk -t8 -c200 -> 184,200 req/s | Latency: 0.4ms", cls: "perf" }
      ]
    }
  };

  function initPlayground() {
    // Supports both .tab-btn (index.php) and .playground-tab (legacy)
    var tabs    = document.querySelectorAll('.tab-btn, .playground-tab');
    var codeView  = document.getElementById('codeView');
    var termOut   = document.getElementById('terminalOutput');
    var runBtn    = document.getElementById('playgroundRunBtn');

    if (!codeView && !termOut) return; // Not on homepage

    function escHtml(str) {
      return str.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
    }

    function syntaxHL(code) {
      if (!code) return '';
      // Token-based placeholder strategy to prevent attribute collision and comments/strings corruption
      var placeholderIdx = 0;
      var placeholders = {};

      // 1. Extract comments first (preserving content)
      var text = code.replace(/(\/\/[^\n]*)/g, function(match) {
        var key = '___TZCMT_' + (placeholderIdx++) + '___';
        placeholders[key] = '<span class="cmt">' + escHtml(match) + '</span>';
        return key;
      });

      // 2. Extract string literals
      text = text.replace(/"([^"\\]*(?:\\.[^"\\]*)*)"/g, function(match) {
        var key = '___TZSTR_' + (placeholderIdx++) + '___';
        placeholders[key] = '<span class="str">' + escHtml(match) + '</span>';
        return key;
      });

      // 3. Escape HTML safely on remaining code tokens
      text = escHtml(text);

      // 4. Highlight keywords
      text = text.replace(/\b(import|fn|let|ret|if|else|while|for|in|say|struct|unsafe|as|sizeof|typedef|extern|break|continue)\b/g, '<span class="kw">$1</span>');

      // 5. Highlight primitive and system types
      text = text.replace(/\b(int|float|str|char|bool|void|u8|i8|u16|ActorSystem|ActorMessage|Arena|TensorData|File)\b/g, '<span class="ty">$1</span>');

      // 6. Highlight numeric literals
      text = text.replace(/\b(\d+(?:\.\d+)?)\b/g, '<span class="num">$1</span>');

      // 7. Restore placeholders
      for (var k in placeholders) {
        text = text.replace(k, placeholders[k]);
      }

      return text;
    }

    function renderDemo(key) {
      var demo = codeDemos[key] || codeDemos.async;
      if (codeView) codeView.innerHTML = syntaxHL(demo.code);
      if (termOut) {
        termOut.innerHTML = demo.output.map(function(line) {
          var cls = '';
          if (line.cls === 'muted')   cls = 'style="color:#64748b"';
          if (line.cls === 'success') cls = 'style="color:#10b981"';
          if (line.cls === 'perf')    cls = 'style="color:#00d2ff"';
          return '<div class="output-line" ' + cls + '>' + escHtml(line.text) + '</div>';
        }).join('');
      }
    }

    // Tab switching
    tabs.forEach(function (tab) {
      tab.addEventListener('click', function () {
        tabs.forEach(function (t) { t.classList.remove('active'); });
        tab.classList.add('active');
        var key = tab.getAttribute('data-tab') || tab.getAttribute('data-demo') || 'async';
        renderDemo(key);
      });
    });

    // Run button animation & live simulator
    if (runBtn) {
      runBtn.addEventListener('click', function () {
        var activeTab = document.querySelector('.tab-btn.active, .playground-tab.active');
        var key = activeTab ? (activeTab.getAttribute('data-tab') || activeTab.getAttribute('data-demo') || 'async') : 'async';
        var badge = document.getElementById('termStatusBadge');
        
        runBtn.disabled = true;
        runBtn.innerHTML = '<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83"/></svg><span>Compiling...</span>';
        if (badge) {
          badge.className = 'badge badge-warning';
          badge.textContent = 'Building...';
        }

        if (termOut) {
          termOut.innerHTML = '<div class="output-line" style="color:#0ea5e9;">[tezzc] Parsing AST &amp; resolving 4D tensor types... (0.8ms)</div>' +
                              '<div class="output-line" style="color:var(--text-tertiary);">[tezzc] Emitting standalone native x64 PE machine code...</div>';
          
          setTimeout(function () {
            renderDemo(key);
            runBtn.disabled = false;
            runBtn.innerHTML = '<svg width="12" height="12" viewBox="0 0 24 24" fill="currentColor"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg><span>Run</span>';
            if (badge) {
              badge.className = 'badge badge-success';
              badge.textContent = 'Exit: 0 (OK)';
            }
          }, 380);
        }
      });
    }

    // Render initial (first) demo
    var firstActive = document.querySelector('.tab-btn.active, .playground-tab.active');
    var initKey = firstActive ? (firstActive.getAttribute('data-tab') || firstActive.getAttribute('data-demo') || 'async') : 'async';
    renderDemo(initKey);
  }

  // ── 6. Benchmark Bar Scroll Animations ───────────────────────────────────────
  function initBenchmarkAnims() {
    var bars = document.querySelectorAll('.bench-bar-fill');
    if (!bars.length) return;

    // Store target widths, reset to 0
    var targets = [];
    bars.forEach(function (bar) {
      targets.push(bar.style.width || '0%');
      bar.style.width = '0%';
      bar.style.transition = 'width 1s cubic-bezier(0.16,1,0.3,1)';
    });

    if ('IntersectionObserver' in window) {
      var obs = new IntersectionObserver(function (entries) {
        entries.forEach(function (entry) {
          if (!entry.isIntersecting) return;
          bars.forEach(function (bar, i) {
            setTimeout(function () { bar.style.width = targets[i]; }, i * 60);
          });
          obs.disconnect();
        });
      }, { threshold: 0.25 });

      var section = document.getElementById('benchmarks') || document.querySelector('.benchmarks-grid');
      if (section) obs.observe(section);
    } else {
      // Fallback: animate immediately
      bars.forEach(function (bar, i) {
        setTimeout(function () { bar.style.width = targets[i]; }, i * 60);
      });
    }
  }

  // ── 7. Feature Card Entrance Animations ──────────────────────────────────────
  function initCardAnims() {
    var cards = document.querySelectorAll('.feature-card, .benchmark-card');
    if (!cards.length || !('IntersectionObserver' in window)) return;

    cards.forEach(function (card) {
      card.style.opacity = '0';
      card.style.transform = 'translateY(20px)';
      card.style.transition = 'opacity .5s ease, transform .5s ease';
    });

    var obs = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        entry.target.style.opacity = '1';
        entry.target.style.transform = 'translateY(0)';
        obs.unobserve(entry.target);
      });
    }, { threshold: 0.1 });

    cards.forEach(function (card) { obs.observe(card); });
  }

  // ── 8. Smooth Anchor Scroll ───────────────────────────────────────────────────
  function initSmoothScroll() {
    document.querySelectorAll('a[href^="#"]').forEach(function (a) {
      a.addEventListener('click', function (e) {
        var href = a.getAttribute('href');
        if (href.length > 1) {
          var target = document.querySelector(href);
          if (target) {
            e.preventDefault();
            target.scrollIntoView({ behavior: 'smooth', block: 'start' });
          }
        }
      });
    });
  }

  // ── 9. Active Nav Link (scroll spy) ──────────────────────────────────────────
  function initScrollSpy() {
    var sections = document.querySelectorAll('section[id]');
    var navLinks = document.querySelectorAll('.nav-link');
    if (!sections.length || !navLinks.length) return;

    var obs = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        var id = entry.target.id;
        navLinks.forEach(function (link) {
          var href = link.getAttribute('href') || '';
          link.classList.toggle('active', href.endsWith('#' + id));
        });
      });
    }, { threshold: 0.4 });

    sections.forEach(function (s) { obs.observe(s); });
  }

  // ── 10. Library / Package Registry Filter ────────────────────────────────────
  function initLibFilter() {
    var searchInput = document.getElementById('pkgSearchInput');
    var chipBtns    = document.querySelectorAll('.chip-btn');
    var cards       = document.querySelectorAll('.package-card');

    if (!cards.length) return;

    var activeCat = 'all';

    function filterCards() {
      var query = searchInput ? searchInput.value.trim().toLowerCase() : '';
      var visible = 0;
      cards.forEach(function (card) {
        var name = (card.getAttribute('data-name') || '').toLowerCase();
        var cat  = (card.getAttribute('data-cat')  || '').toLowerCase();
        var desc = (card.querySelector('.pkg-desc')  ? card.querySelector('.pkg-desc').textContent  : '').toLowerCase();
        var matchCat   = (activeCat === 'all' || cat === activeCat);
        var matchQuery = (!query || name.indexOf(query) !== -1 || desc.indexOf(query) !== -1 || cat.indexOf(query) !== -1);
        var show = matchCat && matchQuery;
        card.style.display = show ? '' : 'none';
        if (show) visible++;
      });

      // Show "no results" message if needed
      var noResult = document.getElementById('pkgNoResult');
      if (noResult) {
        noResult.style.display = visible === 0 ? 'block' : 'none';
      }
    }

    // Chip button clicks
    chipBtns.forEach(function (btn) {
      btn.addEventListener('click', function () {
        chipBtns.forEach(function (b) { b.classList.remove('active'); });
        btn.classList.add('active');
        activeCat = (btn.getAttribute('data-cat') || 'all').toLowerCase();
        filterCards();
      });
    });

    // Search input
    if (searchInput) {
      searchInput.addEventListener('input', filterCards);
      searchInput.addEventListener('search', filterCards);
    }

    // Initial render
    filterCards();
  }

  // ── 11. Auto-inject Copy Buttons on all <pre><code> blocks ───────────────────
  function initCodeBlockCopyButtons() {
    var pres = document.querySelectorAll('pre:not(.has-copy-btn)');
    pres.forEach(function (pre) {
      var code = pre.querySelector('code');
      if (!code) return;

      // Make pre relative for positioning
      pre.style.position = 'relative';
      pre.classList.add('has-copy-btn');

      var btn = document.createElement('button');
      btn.className = 'code-copy-btn copy-btn';
      btn.setAttribute('title', 'Copy code');
      btn.setAttribute('aria-label', 'Copy code to clipboard');
      btn.setAttribute('data-copy', code.textContent || '');
      btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>';
      pre.appendChild(btn);
    });
  }

  // ── 12. Desktop Navbar Dropdown (More) ───────────────────────────────────────
  function initNavDropdown() {
    var moreWrap = document.getElementById('navMoreDropdown');
    var moreBtn  = document.getElementById('navMoreBtn');
    var moreMenu = document.getElementById('navMoreMenu');
    if (!moreWrap || !moreBtn || !moreMenu) return;

    moreBtn.addEventListener('click', function (e) {
      e.preventDefault();
      e.stopPropagation();
      var isOpen = moreWrap.classList.contains('open') || moreMenu.classList.contains('show');
      if (isOpen) {
        moreWrap.classList.remove('open');
        moreMenu.classList.remove('show');
        moreBtn.setAttribute('aria-expanded', 'false');
      } else {
        moreWrap.classList.add('open');
        moreMenu.classList.add('show');
        moreBtn.setAttribute('aria-expanded', 'true');
      }
    });

    document.addEventListener('click', function (e) {
      if (!moreWrap.contains(e.target)) {
        moreWrap.classList.remove('open');
        moreMenu.classList.remove('show');
        moreBtn.setAttribute('aria-expanded', 'false');
      }
    });

    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape' && (moreWrap.classList.contains('open') || moreMenu.classList.contains('show'))) {
        moreWrap.classList.remove('open');
        moreMenu.classList.remove('show');
        moreBtn.setAttribute('aria-expanded', 'false');
      }
    });
  }

  // ── 13. Universal Language Dropdown ──────────────────────────────────────────
  function initLangDropdown() {
    document.addEventListener('click', function (e) {
      var btn = e.target.closest('.lang-selector-btn');
      var allDds = document.querySelectorAll('.lang-dropdown');
      var allBtns = document.querySelectorAll('.lang-selector-btn');

      if (btn) {
        e.preventDefault();
        e.stopPropagation();
        var wrapper = btn.closest('.lang-selector-wrapper');
        var dd = wrapper ? wrapper.querySelector('.lang-dropdown') : null;
        var isOpen = dd && (dd.classList.contains('show') || dd.style.display === 'block');

        allDds.forEach(function (d) { d.classList.remove('show'); d.style.display = 'none'; });
        allBtns.forEach(function (b) { b.setAttribute('aria-expanded', 'false'); });

        if (dd && !isOpen) {
          dd.classList.add('show');
          dd.style.display = 'block';
          btn.setAttribute('aria-expanded', 'true');
        }
        return;
      }

      if (!e.target.closest('.lang-dropdown')) {
        allDds.forEach(function (d) { d.classList.remove('show'); d.style.display = 'none'; });
        allBtns.forEach(function (b) { b.setAttribute('aria-expanded', 'false'); });
      }
    });

    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape') {
        document.querySelectorAll('.lang-dropdown').forEach(function (d) { d.classList.remove('show'); d.style.display = 'none'; });
        document.querySelectorAll('.lang-selector-btn').forEach(function (b) { b.setAttribute('aria-expanded', 'false'); });
      }
    });
  }

  // ── 14. Real-time Documentation & Card Filters ──────────────────────────────
  function initDocFilter() {
    var inputs = document.querySelectorAll('input[data-filter-input], input.tn-search');
    inputs.forEach(function (input) {
      var targetSelector = input.getAttribute('data-filter-input') || '[data-example-card]';
      input.addEventListener('input', function () {
        var q = input.value.trim().toLowerCase();
        var cards = document.querySelectorAll(targetSelector);
        cards.forEach(function (card) {
          var text = (card.textContent || '').toLowerCase();
          card.style.display = (!q || text.indexOf(q) !== -1) ? '' : 'none';
        });
      });
    });
  }

  // ── 15. Form Submit Button Loading State ─────────────────────────────────────
  function initButtonLoaders() {
    document.addEventListener('submit', function (e) {
      var form = e.target;
      if (!form || form.tagName !== 'FORM') return;

      if (typeof form.checkValidity === 'function' && !form.checkValidity()) {
        return;
      }

      var btn = form.querySelector('button[type="submit"], input[type="submit"], .btn-submit, button:not([type="button"]):not([type="reset"])');
      if (!btn || btn.classList.contains('is-loading')) return;

      btn.classList.add('is-loading');

      if (!btn.querySelector('.btn-spinner')) {
        var spinner = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
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
      var trigger = e.target.closest('[data-loading], .btn-action-load');
      if (!trigger || trigger.classList.contains('is-loading')) return;
      trigger.classList.add('is-loading');
      if (!trigger.querySelector('.btn-spinner')) {
        var sp = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
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
  }

  // ── Bootstrap ────────────────────────────────────────────────────────────────
  function boot() {
    initTheme();
    initMobileNav();
    initHeaderScroll();
    initCopyButtons();
    initCodeBlockCopyButtons();
    initPlayground();
    initBenchmarkAnims();
    initCardAnims();
    initSmoothScroll();
    initScrollSpy();
    initLibFilter();
    initNavDropdown();
    initLangDropdown();
    initDocFilter();
    initButtonLoaders();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }

})();
