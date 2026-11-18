const copyStatus = document.getElementById("copy-status");

document.querySelectorAll(".copy-button").forEach((button) => {
  let resetTimer;
  button.addEventListener("click", async () => {
    clearTimeout(resetTimer);
    copyStatus.textContent = "";
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
      button.textContent = "Copied";
      copyStatus.textContent = "Copied to clipboard.";
    } catch {
      button.textContent = "Try again";
      copyStatus.textContent = "Copy failed. Select the code and copy it manually.";
    }
    resetTimer = setTimeout(() => { button.textContent = "Copy"; }, 1800);
  });
});
