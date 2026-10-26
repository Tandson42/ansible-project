const exploreButton = document.querySelector("#explore-button");
const themeToggle = document.querySelector("#theme-toggle");
const root = document.documentElement;

exploreButton?.addEventListener("click", () => {
	exploreButton.textContent = "Diretórios disponíveis em breve";
	exploreButton.disabled = true;
});

themeToggle?.addEventListener("click", () => {
	const isNightMode = root.dataset.theme !== "light";
	root.dataset.theme = isNightMode ? "light" : "dark";
	themeToggle.setAttribute("aria-pressed", String(!isNightMode));
	themeToggle.textContent = isNightMode ? "🌙 Modo noturno" : "☀️ Modo claro";
});

const systemValues = {
	os: navigator.userAgentData?.platform || navigator.platform || "Não informado",
	browser: navigator.userAgent,
	language: navigator.language || "Não informado",
	timezone: Intl.DateTimeFormat().resolvedOptions().timeZone || "Não informado",
	cores: navigator.hardwareConcurrency ? `${navigator.hardwareConcurrency} núcleos` : "Não informado",
	memory: navigator.deviceMemory ? `${navigator.deviceMemory} GB (estimada)` : "Não disponível",
	connection: navigator.onLine ? "Online" : "Offline",
	screen: `${window.screen.width} × ${window.screen.height} px`,
};

Object.entries(systemValues).forEach(([key, value]) => {
	const element = document.querySelector(`[data-system="${key}"]`);
	if (element) {
		element.textContent = value;
	}
});
