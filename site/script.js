document.querySelectorAll(".copy-button").forEach((button) => {
  button.addEventListener("click", async () => {
    try {
      if (navigator.clipboard?.writeText) {
        await navigator.clipboard.writeText(button.dataset.copy);
      } else {
        const field = document.createElement("textarea");
        field.value = button.dataset.copy;
        field.style.position = "fixed";
        field.style.opacity = "0";
        document.body.append(field);
        field.select();
        const copied = document.execCommand("copy");
        field.remove();
        if (!copied) throw new Error("Copy failed");
      }
      const label = button.textContent;
      button.textContent = "Copied ✓";
      setTimeout(() => { button.textContent = label; }, 1800);
    } catch {
      button.textContent = "Copy failed";
    }
  });
});
