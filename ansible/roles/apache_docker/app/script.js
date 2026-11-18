const exploreButton = document.querySelector("#explore-button");
const themeToggle = document.querySelector("#theme-toggle");
const dbRefreshButton = document.querySelector("#db-refresh");
const dbBadge = document.querySelector("#db-badge");
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

function setDbField(key, value) {
	const element = document.querySelector(`[data-db="${key}"]`);
	if (element) {
		element.textContent = value;
	}
}

async function checkDbConnection() {
	if (dbBadge) {
		dbBadge.className = "status-badge status-pending";
		dbBadge.textContent = "🔄 Verificando MySQL...";
	}
	if (dbRefreshButton) {
		dbRefreshButton.disabled = true;
	}

	try {
		const response = await fetch("/db.php", { cache: "no-store" });
		const data = await response.json();

		setDbField("node", data.node || "Não informado");
		setDbField("endpoint", data.db ? `${data.db.host}:${data.db.port}` : "Não informado");
		setDbField("database", data.db?.database || "Não informado");

		if (response.ok && data.status === "ok") {
			if (dbBadge) {
				dbBadge.className = "status-badge";
				dbBadge.textContent = "✅ MySQL Conectado";
			}
			setDbField("status", "Conectado");
			setDbField("version", data.db?.version || "Não informado");
			setDbField("server_time", data.db?.server_time || "Não informado");
		} else {
			if (dbBadge) {
				dbBadge.className = "status-badge status-error";
				dbBadge.textContent = "❌ Falha na Conexão MySQL";
			}
			setDbField("status", `Erro: ${data.error || "Falha ao conectar"}`);
			setDbField("version", "Indisponível");
			setDbField("server_time", "Indisponível");
		}
	} catch (error) {
		if (dbBadge) {
			dbBadge.className = "status-badge status-error";
			dbBadge.textContent = "❌ Erro ao consultar /db.php";
		}
		setDbField("node", "Indisponível");
		setDbField("status", `Erro: ${error.message}`);
		setDbField("endpoint", "Indisponível");
		setDbField("database", "Indisponível");
		setDbField("version", "Indisponível");
		setDbField("server_time", "Indisponível");
	} finally {
		if (dbRefreshButton) {
			dbRefreshButton.disabled = false;
		}
	}
}

dbRefreshButton?.addEventListener("click", checkDbConnection);
checkDbConnection();
