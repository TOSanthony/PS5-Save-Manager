async function main() {
    const PAYLOAD = window.workingDir + '/lapy_jb_daemon.elf';

    return {
        mainText: "lapy Jb daemon",
        secondaryText: 'lapy Jb daemon',
	onclick: async () => {
	    return {
		path: PAYLOAD,
                daemon: true
	    };
        }
    };
}
