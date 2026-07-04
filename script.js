(function () {
  const navLinks = Array.from(document.querySelectorAll(".toc a"));
  const sections = Array.from(document.querySelectorAll(".doc-section"));
  const searchInput = document.querySelector("#doc-search");

  document.querySelectorAll("pre").forEach((block) => {
    const code = block.querySelector("code");
    if (!code) {
      return;
    }

    const button = document.createElement("button");
    button.className = "copy-code";
    button.type = "button";
    button.textContent = "Copy";
    button.addEventListener("click", async () => {
      const text = code.innerText;
      try {
        await navigator.clipboard.writeText(text);
        button.textContent = "Copied";
      } catch (error) {
        const range = document.createRange();
        range.selectNodeContents(code);
        const selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
        button.textContent = "Select";
      }

      window.setTimeout(() => {
        button.textContent = "Copy";
      }, 1600);
    });
    block.appendChild(button);
  });

  const observer = new IntersectionObserver(
    (entries) => {
      const visible = entries
        .filter((entry) => entry.isIntersecting)
        .sort((a, b) => b.intersectionRatio - a.intersectionRatio)[0];

      if (!visible) {
        return;
      }

      navLinks.forEach((link) => {
        link.classList.toggle("is-active", link.hash === `#${visible.target.id}`);
      });
    },
    {
      rootMargin: "-20% 0px -65% 0px",
      threshold: [0.1, 0.35, 0.6]
    }
  );

  sections.forEach((section) => observer.observe(section));

  if (searchInput) {
    searchInput.addEventListener("input", () => {
      const query = searchInput.value.trim().toLowerCase();

      sections.forEach((section) => {
        const haystack = `${section.dataset.search || ""} ${section.innerText}`.toLowerCase();
        const matched = !query || haystack.includes(query);
        section.classList.toggle("is-hidden", !matched);
      });

      navLinks.forEach((link) => {
        const target = document.querySelector(link.hash);
        link.hidden = Boolean(target && target.classList.contains("is-hidden"));
      });
    });
  }
})();
